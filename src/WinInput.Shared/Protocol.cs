using System;
using System.IO;
using System.Text;

namespace WinInput.Shared
{
    public static class Protocol
    {
        public const int MaxFrameBytes = 65536;
        public static void Write(Stream stream, string secret, string command, string value)
        {
            if (secret == null || secret.Length < 16) throw new ArgumentException("共享密钥至少16个字符");
            var body = secret + "\n" + command + "\n" + (value ?? "");
            var bytes = Encoding.UTF8.GetBytes(body);
            if (bytes.Length > MaxFrameBytes) throw new ArgumentException("消息过大");
            var length = BitConverter.GetBytes(bytes.Length);
            stream.Write(length, 0, length.Length); stream.Write(bytes, 0, bytes.Length); stream.Flush();
        }
        public static string[] Read(Stream stream)
        {
            var header = ReadExact(stream, 4); if (header == null) return null;
            var length = BitConverter.ToInt32(header, 0);
            if (length < 1 || length > MaxFrameBytes) throw new InvalidDataException("消息长度无效");
            var bytes = ReadExact(stream, length); if (bytes == null) throw new EndOfStreamException();
            return Encoding.UTF8.GetString(bytes).Split(new[] { '\n' }, 3);
        }
        static byte[] ReadExact(Stream stream, int count)
        {
            var result = new byte[count]; var offset = 0; int read;
            while (offset < count && (read = stream.Read(result, offset, count - offset)) > 0) offset += read;
            return offset == 0 ? null : (offset == count ? result : null);
        }
    }
}