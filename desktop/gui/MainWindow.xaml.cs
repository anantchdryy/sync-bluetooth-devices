using Microsoft.Win32;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Windows;
using System.Windows.Threading;

namespace TandemAudio.Desktop;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<DiscoveredRoom> rooms = [];
    private readonly ObservableCollection<DeviceRow> devices = [];
    private readonly DispatcherTimer refresh = new() { Interval = TimeSpan.FromSeconds(1) };
    private readonly string diagnostics = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "TandemAudio", "Logs");
    private readonly RoomDiscovery discovery = new();
    private Process? roomProcess;
    private bool isHost;
    private string? trackPath;
    private int trackSampleRate;

    public MainWindow()
    {
        InitializeComponent();
        RoomsList.ItemsSource = rooms;
        DevicesList.ItemsSource = devices;
        Directory.CreateDirectory(diagnostics);
        discovery.RoomsChanged += found => Dispatcher.Invoke(() => {
            rooms.Clear();
            foreach (var room in found) rooms.Add(room);
            DiscoveryStatus.Text = rooms.Count == 0
                ? "No rooms found. Check that both devices share a Wi-Fi network."
                : $"{rooms.Count} nearby room{(rooms.Count == 1 ? "" : "s")}";
        });
        discovery.StatusChanged += status => Dispatcher.Invoke(() => DiscoveryStatus.Text = status);
        discovery.Start();
        refresh.Tick += async (_, _) => await RefreshDevicesAsync();
        refresh.Start();
        Closing += (_, _) => {
            discovery.Dispose();
            refresh.Stop();
            if (roomProcess is { HasExited: false }) roomProcess.Kill(entireProcessTree: true);
            roomProcess?.Dispose();
        };
    }

    private void BrowseTrack_Click(object sender, RoutedEventArgs e)
    {
        var picker = new OpenFileDialog { Filter = "WAV audio (*.wav)|*.wav", CheckFileExists = true };
        if (picker.ShowDialog(this) == true) {
            trackPath = picker.FileName;
            TrackPathBox.Text = trackPath;
        }
    }

    private void CreateRoom_Click(object sender, RoutedEventArgs e)
    {
        if (roomProcess is { HasExited: false }) { ShowError("Close the current room first."); return; }
        if (trackPath is null || !File.Exists(trackPath)) { ShowError("Choose a WAV file first."); return; }
        var name = RoomNameBox.Text.Trim();
        if (name.Length is < 1 or > 48 || name.Any(c => c is < ' ' or > '~')) {
            ShowError("Use a room name with 1 to 48 ordinary characters."); return;
        }
        if (!int.TryParse(OutputLatencyBox.Text, out var latency) || latency is < -1000 or > 1000) {
            ShowError("Output delay must be between -1000 and 1000 milliseconds."); return;
        }
        try { trackSampleRate = WavHeader.ReadSampleRate(trackPath); }
        catch (Exception error) { ShowError($"Cannot read the WAV file: {error.Message}"); return; }
        var info = NewEngineProcess();
        info.ArgumentList.Add("host");
        info.ArgumentList.Add(trackPath);
        info.ArgumentList.Add("--room-name");
        info.ArgumentList.Add(name);
        info.ArgumentList.Add("--output-latency-ms");
        info.ArgumentList.Add(latency.ToString(CultureInfo.InvariantCulture));
        StartRoomProcess(info, name, Path.GetFileName(trackPath), host: true);
    }

    private void JoinRoom_Click(object sender, RoutedEventArgs e)
    {
        if (roomProcess is { HasExited: false }) { ShowError("Close the current room first."); return; }
        if (RoomsList.SelectedItem is not DiscoveredRoom room) { ShowError("Choose a nearby room first."); return; }
        var info = NewEngineProcess();
        info.ArgumentList.Add("room-client");
        info.ArgumentList.Add(room.Address);
        info.ArgumentList.Add("--session-port");
        info.ArgumentList.Add(room.Port.ToString(CultureInfo.InvariantCulture));
        StartRoomProcess(info, room.DisplayName, "Listening to host", host: false);
    }

    private ProcessStartInfo NewEngineProcess()
    {
        var engine = Path.Combine(AppContext.BaseDirectory, "syncaudio.exe");
        if (!File.Exists(engine)) throw new FileNotFoundException("The audio engine is missing from this installation.", engine);
        return new ProcessStartInfo(engine) {
            UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8
        };
    }

    private void StartRoomProcess(ProcessStartInfo info, string name, string track, bool host)
    {
        try {
            var process = new Process { StartInfo = info, EnableRaisingEvents = true };
            var logPath = Path.Combine(diagnostics, $"room-{DateTime.UtcNow:yyyyMMdd-HHmmss}.log");
            process.OutputDataReceived += (_, args) => { if (args.Data is not null) File.AppendAllText(logPath, args.Data + Environment.NewLine); };
            process.ErrorDataReceived += (_, args) => { if (args.Data is not null) File.AppendAllText(logPath, args.Data + Environment.NewLine); };
            process.Exited += (_, _) => Dispatcher.Invoke(() => {
                RoomStatus.Text = process.ExitCode == 0 ? "Room closed" : "Audio stopped. See diagnostics for details.";
                devices.Clear();
            });
            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            roomProcess = process;
            isHost = host;
            ActiveRoomName.Text = name;
            CurrentTrack.Text = track;
            RoomStatus.Text = host ? "Hosting room" : "Joining room";
            Pages.SelectedIndex = 1;
        } catch (Exception error) { ShowError($"Cannot start audio: {error.Message}"); }
    }

    private async Task SendControlAsync(string command)
    {
        if (!isHost || roomProcess is not { HasExited: false }) { ShowError("Create a room here to use playback controls."); return; }
        try {
            using var socket = new TcpClient();
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
            await socket.ConnectAsync("127.0.0.1", 40102, timeout.Token);
            using var writer = new StreamWriter(socket.GetStream(), Encoding.ASCII) { AutoFlush = true };
            using var reader = new StreamReader(socket.GetStream(), Encoding.ASCII);
            await writer.WriteLineAsync(command);
            var response = await reader.ReadLineAsync(timeout.Token);
            RoomStatus.Text = response?.StartsWith("SCHEDULED ") == true
                ? $"{command} scheduled for the room" : $"Room control: {response ?? "no response"}";
        } catch (Exception error) { ShowError($"Room control failed: {error.Message}"); }
    }

    private async void Play_Click(object sender, RoutedEventArgs e) => await SendControlAsync("PLAY");
    private async void Pause_Click(object sender, RoutedEventArgs e) => await SendControlAsync("PAUSE");
    private async void Stop_Click(object sender, RoutedEventArgs e) => await SendControlAsync("STOP");
    private async void Seek_Click(object sender, RoutedEventArgs e)
    {
        if (!double.TryParse(SeekSecondsBox.Text, CultureInfo.CurrentCulture, out var seconds) ||
            !double.IsFinite(seconds) || seconds < 0 || seconds > 86400 || trackSampleRate == 0) {
            ShowError("Enter a valid position in seconds."); return;
        }
        await SendControlAsync($"SEEK {(ulong)(seconds * trackSampleRate)}");
    }

    private async Task RefreshDevicesAsync()
    {
        if (!isHost || roomProcess is not { HasExited: false }) return;
        try {
            using var socket = new TcpClient();
            using var timeout = new CancellationTokenSource(TimeSpan.FromMilliseconds(700));
            await socket.ConnectAsync("127.0.0.1", 40102, timeout.Token);
            using var writer = new StreamWriter(socket.GetStream(), Encoding.ASCII) { AutoFlush = true };
            using var reader = new StreamReader(socket.GetStream(), Encoding.ASCII);
            await writer.WriteLineAsync("MEMBERS");
            var next = new List<DeviceRow>();
            for (var index = 0; index < 32; index++) {
                var line = await reader.ReadLineAsync(timeout.Token);
                if (line is null || line == "END") break;
                var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length < 7 || parts[0] != "MEMBER") continue;
                var loss = double.TryParse(parts[4], CultureInfo.InvariantCulture, out var value) ? value : 100;
                next.Add(new DeviceRow(parts[1], parts[2],
                    parts[2] == "RECONNECTING" ? "Reconnecting" : loss > 5 ? "Unstable" : loss > 1 ? "Good" : "Excellent",
                    "Device output"));
            }
            devices.Clear();
            foreach (var device in next) devices.Add(device);
        } catch { /* Host may be starting or closing. */ }
    }

    private void OpenDiagnostics_Click(object sender, RoutedEventArgs e) =>
        Process.Start(new ProcessStartInfo("explorer.exe", diagnostics) { UseShellExecute = true });

    private void ShowError(string message) => MessageBox.Show(this, message, "Tandem Audio", MessageBoxButton.OK, MessageBoxImage.Information);
}

public sealed record DeviceRow(string Name, string State, string Quality, string Output);
