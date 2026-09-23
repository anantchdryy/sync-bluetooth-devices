using System.Buffers.Binary;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;

namespace TandemAudio.Desktop;

public sealed record DiscoveredRoom(string Id, string DisplayName, string Address, ushort Port);

/// <summary>Small DNS-SD browser for the existing _tandemaudio._tcp LAN service.</summary>
internal sealed class RoomDiscovery : IDisposable
{
    private const string Service = "_tandemaudio._tcp.local";
    private static readonly IPEndPoint Multicast = new(IPAddress.Parse("224.0.0.251"), 5353);
    private readonly CancellationTokenSource cancellation = new();
    private readonly Dictionary<string, (string Name, string Address, ushort Port, DateTime Seen)> known = [];
    private readonly HashSet<string> services = [];
    private readonly Dictionary<string, (string Host, ushort Port)> targets = [];
    private readonly Dictionary<string, string> addresses = [];
    private readonly Dictionary<string, string> identifiers = [];
    private UdpClient? socket;
    private IReadOnlyList<IPAddress> interfaces = [];

    public event Action<IReadOnlyList<DiscoveredRoom>>? RoomsChanged;
    public event Action<string>? StatusChanged;

    public void Start() => _ = RunAsync(cancellation.Token);

    private async Task RunAsync(CancellationToken token)
    {
        try {
            socket = new UdpClient(AddressFamily.InterNetwork);
            socket.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
            socket.Client.Bind(new IPEndPoint(IPAddress.Any, 5353));
            RefreshInterfaces();
            var lastQuery = DateTime.MinValue;
            while (!token.IsCancellationRequested) {
                if (DateTime.UtcNow - lastQuery >= TimeSpan.FromSeconds(10)) {
                    RefreshInterfaces();
                    foreach (var address in interfaces) {
                        try {
                            socket.Client.SetSocketOption(SocketOptionLevel.IP,
                                SocketOptionName.MulticastInterface, address.GetAddressBytes());
                            await socket.SendAsync(Query(), Multicast, token);
                        } catch (SocketException) { /* Try other active interfaces. */ }
                    }
                    lastQuery = DateTime.UtcNow;
                    Expire();
                }
                try {
                    using var wait = CancellationTokenSource.CreateLinkedTokenSource(token);
                    wait.CancelAfter(TimeSpan.FromSeconds(1));
                    var result = await socket.ReceiveAsync(wait.Token);
                    Parse(result.Buffer);
                } catch (OperationCanceledException) when (!token.IsCancellationRequested) { }
            }
        } catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        catch (Exception error) { StatusChanged?.Invoke($"Discovery unavailable: {error.Message}"); }
    }

    private void RefreshInterfaces()
    {
        if (socket is null) return;
        var active = NetworkInterface.GetAllNetworkInterfaces()
            .Where(nic => nic.OperationalStatus == OperationalStatus.Up &&
                          nic.NetworkInterfaceType != NetworkInterfaceType.Loopback &&
                          nic.SupportsMulticast)
            .SelectMany(nic => nic.GetIPProperties().UnicastAddresses)
            .Select(entry => entry.Address)
            .Where(address => address.AddressFamily == AddressFamily.InterNetwork &&
                              !IPAddress.IsLoopback(address) &&
                              !address.ToString().StartsWith("169.254.", StringComparison.Ordinal))
            .Distinct().ToArray();
        foreach (var address in active.Except(interfaces)) {
            try { socket.JoinMulticastGroup(Multicast.Address, address); }
            catch (SocketException) { /* One unavailable adapter must not hide other rooms. */ }
        }
        interfaces = active;
        if (active.Length == 0) StatusChanged?.Invoke("No active LAN connection found.");
    }

    private static byte[] Query()
    {
        var message = new List<byte>(64);
        message.AddRange(new byte[] { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 });
        foreach (var label in Service.Split('.')) {
            message.Add((byte)label.Length);
            message.AddRange(Encoding.ASCII.GetBytes(label));
        }
        message.AddRange(new byte[] { 0, 0, 12, 0, 1 });
        return [.. message];
    }

    private static ushort U16(ReadOnlySpan<byte> bytes, int offset) =>
        BinaryPrimitives.ReadUInt16BigEndian(bytes.Slice(offset, 2));

    private static uint U32(ReadOnlySpan<byte> bytes, int offset) =>
        BinaryPrimitives.ReadUInt32BigEndian(bytes.Slice(offset, 4));

    private static string Name(ReadOnlySpan<byte> bytes, ref int offset)
    {
        var labels = new List<string>();
        var position = offset;
        var jumped = false;
        for (var count = 0; count < 32; count++) {
            if (position >= bytes.Length) throw new FormatException("DNS name is truncated.");
            var length = bytes[position++];
            if (length == 0) { if (!jumped) offset = position; return string.Join('.', labels); }
            if ((length & 0xC0) == 0xC0) {
                if (position >= bytes.Length) throw new FormatException("DNS pointer is truncated.");
                var target = ((length & 0x3F) << 8) | bytes[position++];
                if (target >= bytes.Length) throw new FormatException("DNS pointer is invalid.");
                if (!jumped) offset = position;
                position = target;
                jumped = true;
                continue;
            }
            if (length > 63 || position + length > bytes.Length) throw new FormatException("DNS label is invalid.");
            labels.Add(Encoding.UTF8.GetString(bytes.Slice(position, length)));
            position += length;
        }
        throw new FormatException("DNS name has too many labels.");
    }

    private void Parse(byte[] packet)
    {
        try {
            var bytes = packet.AsSpan();
            if (bytes.Length < 12 || (U16(bytes, 2) & 0x8000) == 0) return;
            var questions = U16(bytes, 4);
            var records = U16(bytes, 6) + U16(bytes, 8) + U16(bytes, 10);
            if (questions > 32 || records > 128) return;
            var cursor = 12;
            for (var i = 0; i < questions; i++) {
                _ = Name(bytes, ref cursor);
                cursor += 4;
                if (cursor > bytes.Length) return;
            }
            for (var i = 0; i < records; i++) {
                var owner = Name(bytes, ref cursor);
                if (cursor + 10 > bytes.Length) return;
                var type = U16(bytes, cursor);
                var ttl = U32(bytes, cursor + 4);
                var length = U16(bytes, cursor + 8);
                cursor += 10;
                var end = cursor + length;
                if (end > bytes.Length) return;
                if (ttl == 0) { cursor = end; continue; }
                if (type == 12 && owner.Equals(Service, StringComparison.OrdinalIgnoreCase)) {
                    var at = cursor;
                    services.Add(Name(bytes, ref at));
                } else if (type == 33 && length >= 6) {
                    var at = cursor + 6;
                    targets[owner] = (Name(bytes, ref at), U16(bytes, cursor + 4));
                } else if (type == 1 && length == 4) {
                    addresses[owner] = new IPAddress(bytes.Slice(cursor, 4)).ToString();
                } else if (type == 16) {
                    var at = cursor;
                    while (at < end) {
                        var size = bytes[at++];
                        if (at + size > end) break;
                        var text = Encoding.UTF8.GetString(bytes.Slice(at, size));
                        if (text.StartsWith("room=", StringComparison.OrdinalIgnoreCase)) identifiers[owner] = text[5..];
                        at += size;
                    }
                }
                cursor = end;
            }
            var changed = false;
            foreach (var service in services) {
                if (!targets.TryGetValue(service, out var target) ||
                    !addresses.TryGetValue(target.Host, out var address)) continue;
                var id = identifiers.GetValueOrDefault(service, service);
                var display = service.EndsWith("." + Service, StringComparison.OrdinalIgnoreCase)
                    ? service[..^(Service.Length + 1)] : service;
                known[id] = (display, address, target.Port, DateTime.UtcNow);
                changed = true;
            }
            if (changed) Publish();
        } catch (FormatException) { /* Ignore malformed LAN announcements. */ }
          catch (ArgumentOutOfRangeException) { /* Ignore truncated packets. */ }
    }

    private void Expire()
    {
        var expired = known.Where(entry => DateTime.UtcNow - entry.Value.Seen > TimeSpan.FromMinutes(2))
            .Select(entry => entry.Key).ToArray();
        foreach (var key in expired) known.Remove(key);
        if (expired.Length > 0) Publish();
    }

    private void Publish() => RoomsChanged?.Invoke(known.Select(entry =>
        new DiscoveredRoom(entry.Key, entry.Value.Name, entry.Value.Address, entry.Value.Port))
        .OrderBy(room => room.DisplayName).ToArray());

    public void Dispose()
    {
        cancellation.Cancel();
        socket?.Dispose();
        cancellation.Dispose();
    }
}
