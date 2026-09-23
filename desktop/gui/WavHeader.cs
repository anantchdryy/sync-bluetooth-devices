using System.IO;
using System.Text;

namespace TandemAudio.Desktop;

internal static class WavHeader
{
    public static int ReadSampleRate(string path)
    {
        using var file = File.OpenRead(path);
        using var reader = new BinaryReader(file, Encoding.ASCII);
        if (new string(reader.ReadChars(4)) != "RIFF") throw new InvalidDataException("Expected RIFF audio.");
        reader.ReadUInt32();
        if (new string(reader.ReadChars(4)) != "WAVE") throw new InvalidDataException("Expected WAVE audio.");
        while (file.Position + 8 <= file.Length) {
            var name = new string(reader.ReadChars(4));
            var size = reader.ReadUInt32();
            var next = file.Position + size + (size & 1);
            if (next > file.Length) throw new InvalidDataException("WAV chunk exceeds file length.");
            if (name == "fmt " && size >= 16) {
                var encoding = reader.ReadUInt16();
                var channels = reader.ReadUInt16();
                var sampleRate = reader.ReadUInt32();
                if (encoding != 1 || channels is < 1 or > 2 || sampleRate is < 8000 or > 192000)
                    throw new InvalidDataException("Use an 8–192 kHz mono or stereo PCM WAV file.");
                return checked((int)sampleRate);
            }
            file.Position = next;
        }
        throw new InvalidDataException("WAV format is missing.");
    }
}
