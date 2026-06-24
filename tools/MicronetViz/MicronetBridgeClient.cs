using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace MicronetViz;

public sealed class MicronetBridgeClient
{
    private const byte MaxItems = 32;
    private bool _resolverInstalled;

    public bool TryInitialize(out string status)
    {
        EnsureResolver();

        try
        {
            if (NativeMethods.mnviz_is_initialized())
            {
                status = "Bridge uz bezal";
                return true;
            }

            var config = new NativeInitConfig
            {
                NodeName = "MicronetViz",
                StunHost = string.Empty,
                StunPort = 0,
                LocalPort = 33477,
                HeartbeatMs = 5000,
                OfflineTimeoutMs = 15000,
                RetryIntervalMs = 2000,
                RetryCount = 3,
                MaxNodes = 16,
                MaxVars = 16,
                MaxPending = 8,
                LogLevel = 1,
                NodePrivkey = BuildKeyMaterial(),
            };

            var err = NativeMethods.mnviz_init(ref config);
            if (err != 0)
            {
                status = $"Bridge init err={err}";
                return false;
            }

            NativeMethods.mnviz_publish_text("viz.mode", "wpf-bridge");
            NativeMethods.mnviz_publish_text("viz.started", DateTime.Now.ToString("O"));
            status = "Bridge aktivny";
            return true;
        }
        catch (Exception ex)
        {
            status = $"Bridge nedostupny: {ex.GetType().Name}";
            return false;
        }
    }

    public bool Tick()
    {
        try
        {
            return NativeMethods.mnviz_tick() == 0;
        }
        catch
        {
            return false;
        }
    }

    public int PublishText(string key, string value)
    {
        return NativeMethods.mnviz_publish_text(key, value);
    }

    public int UpdateText(string key, string value)
    {
        return NativeMethods.mnviz_update_text(key, value);
    }

    public int SendCustom(byte[] nodeId, byte msgType, string payload)
    {
        var bytes = Encoding.UTF8.GetBytes(payload);
        return NativeMethods.mnviz_send_custom(nodeId, msgType, bytes, (uint)bytes.Length);
    }

    public int BroadcastCustom(byte[] groupHash, byte msgType, string payload)
    {
        var bytes = Encoding.UTF8.GetBytes(payload);
        return NativeMethods.mnviz_broadcast_custom(groupHash, msgType, bytes, (uint)bytes.Length);
    }

    public int GroupCreate(out byte[] groupHash, out byte[] groupKey)
    {
        groupHash = new byte[16];
        groupKey = new byte[16];
        return NativeMethods.mnviz_group_create(groupHash, groupKey);
    }

    public int GroupJoin(byte[] groupHash, byte[] groupKey)
    {
        return NativeMethods.mnviz_group_join(groupHash, groupKey);
    }

    public int GroupLeave(byte[] groupHash)
    {
        return NativeMethods.mnviz_group_leave(groupHash);
    }

    public BridgeSnapshot? GetSnapshot()
    {
        try
        {
            var snapshot = new NativeSnapshot();
            if (NativeMethods.mnviz_snapshot(ref snapshot) != 0)
            {
                return null;
            }

            var nodes = new NativeNode[MaxItems];
            var vars = new NativeVar[MaxItems];
            var messages = new NativeMessage[64];
            byte nodeCount = 0;
            byte varCount = 0;
            byte messageCount = 0;

            if (NativeMethods.mnviz_copy_nodes(nodes, MaxItems, ref nodeCount) != 0)
            {
                nodeCount = 0;
            }
            if (NativeMethods.mnviz_copy_vars(vars, MaxItems, ref varCount) != 0)
            {
                varCount = 0;
            }
            if (NativeMethods.mnviz_copy_messages(messages, 64, ref messageCount) != 0)
            {
                messageCount = 0;
            }

            return new BridgeSnapshot(
                snapshot.NodeCount,
                snapshot.OnlineCount,
                snapshot.GroupCount,
                MapMetrics(snapshot.LocalMetrics),
                nodes.Take(nodeCount).Select(MapNode).ToList(),
                vars.Take(varCount).Select(MapVar).ToList(),
                messages.Take(messageCount).Select(MapMessage).ToList());
        }
        catch
        {
            return null;
        }
    }

    private void EnsureResolver()
    {
        if (_resolverInstalled)
        {
            return;
        }

        NativeLibrary.SetDllImportResolver(typeof(MicronetBridgeClient).Assembly, ResolveBridgeLibrary);
        _resolverInstalled = true;
    }

    private static IntPtr ResolveBridgeLibrary(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, "micronet_bridge", StringComparison.OrdinalIgnoreCase))
        {
            return IntPtr.Zero;
        }

        var baseDir = AppContext.BaseDirectory;
        var candidates = new[]
        {
            Path.Combine(baseDir, "micronet_bridge.dll"),
            Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", "..", "..", "build", "Debug", "micronet_bridge.dll")),
            Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", "..", "..", "build", "Release", "micronet_bridge.dll")),
        };

        foreach (var candidate in candidates)
        {
            if (File.Exists(candidate) && NativeLibrary.TryLoad(candidate, out var handle))
            {
                return handle;
            }
        }

        return IntPtr.Zero;
    }

    private static byte[] BuildKeyMaterial()
    {
        // Stable private key seed per machine so the app node ID remains stable across restarts.
        var machineSeed = $"{Environment.MachineName}|MicronetViz|v1";
        return SHA256.HashData(Encoding.UTF8.GetBytes(machineSeed));
    }

    private static BridgeMetrics MapMetrics(NativeMetrics metrics)
    {
        return new BridgeMetrics(
            metrics.UptimeS,
            metrics.FreeHeap,
            metrics.ConnectedNodes,
            metrics.GroupCount,
            metrics.PacketsSent,
            metrics.PacketsRecv,
            metrics.Errors,
            metrics.HealthScore);
    }

    private static BridgeNode MapNode(NativeNode node)
    {
        var name = node.IsSelf ? "Self" : $"Node {ToShortHex(node.NodeId)}";
        var lastSeenSeconds = (long)node.LastSeen / 1000L;
        var groups = Enumerable.Range(0, node.GroupCount)
            .Select(i => ToGroupHex(node.GroupHashes, i * 16))
            .ToArray();
        return new BridgeNode(
            name,
            ToShortHex(node.NodeId),
            node.IsOnline,
            node.IsSelf,
            Math.Max(0, (int)(DateTimeOffset.UtcNow.ToUnixTimeSeconds() - lastSeenSeconds)),
            node.GroupCount,
            node.IsAuthorized,
            groups,
            node.PacketsSent,
            node.PacketsRecv,
            node.HealthScore,
            node.FreeHeap,
            node.NodeId.ToArray());
    }

    private static BridgeVar MapVar(NativeVar value)
    {
        var updatedAtSeconds = (long)value.UpdatedAt / 1000L;
        return new BridgeVar(
            "Self",
            ReadAnsiString(value.Key),
            MapTypeName(value.Type),
            DecodeValue(value.Data, (int)value.DataLen),
            Math.Max(0, (int)(DateTimeOffset.UtcNow.ToUnixTimeSeconds() - updatedAtSeconds)));
    }

    private static BridgeMessage MapMessage(NativeMessage message)
    {
        var source = message.SourceIsEmpty ? "-" : $"Node {ToShortHex(message.Src)}";
        var destination = message.DestinationIsEmpty ? "-" : $"Node {ToShortHex(message.Dst)}";
        var payload = DecodeValue(message.Payload, (int)message.PayloadLen);
        var kind = message.Direction switch
        {
            1 => "publish local",
            2 => "node event",
            _ => $"custom #{message.MessageType}",
        };

        return new BridgeMessage(
            DateTimeOffset.FromUnixTimeSeconds(message.Timestamp).LocalDateTime,
            source,
            destination,
            kind,
            payload.Length > 42 ? payload[..42] : payload,
            payload);
    }

    private static string ToShortHex(byte[] bytes)
    {
        return string.Join(':', bytes.Take(4).Select(static b => b.ToString("x2")));
    }

    private static string ToGroupHex(byte[] bytes, int offset)
    {
        if (bytes.Length < offset + 16)
        {
            return string.Empty;
        }

        return Convert.ToHexString(bytes, offset, 16).ToLowerInvariant();
    }

    private static string MapTypeName(byte type) => type switch
    {
        0 => "kv",
        1 => "table",
        2 => "timeseries",
        3 => "var",
        4 => "metric",
        _ => $"type-{type}",
    };

    private static string DecodeValue(byte[] data, int length)
    {
        if (length <= 0)
        {
            return string.Empty;
        }

        var text = Encoding.UTF8.GetString(data, 0, Math.Min(length, data.Length)).TrimEnd('\0');
        if (text.All(ch => !char.IsControl(ch) || ch == '\r' || ch == '\n' || ch == '\t'))
        {
            return text;
        }

        return Convert.ToHexString(data, 0, Math.Min(length, 16));
    }

    private static string ReadAnsiString(byte[] bytes)
    {
        var zeroIndex = Array.IndexOf(bytes, (byte)0);
        var length = zeroIndex >= 0 ? zeroIndex : bytes.Length;
        return Encoding.ASCII.GetString(bytes, 0, length);
    }

    private static class NativeMethods
    {
        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_init(ref NativeInitConfig config);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_tick();

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal static extern bool mnviz_is_initialized();

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_publish_text(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string key,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string value);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_update_text(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string key,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string value);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_send_custom(
            [In] byte[] nodeId,
            byte msgType,
            [In] byte[] payload,
            uint payloadLen);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_broadcast_custom(
            [In] byte[] groupHash,
            byte msgType,
            [In] byte[] payload,
            uint payloadLen);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_group_create(
            [Out] byte[] groupHash,
            [Out] byte[] groupKey);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_group_join(
            [In] byte[] groupHash,
            [In] byte[] groupKey);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_group_leave(
            [In] byte[] groupHash);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_snapshot(ref NativeSnapshot snapshot);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_copy_nodes(
            [Out] NativeNode[] nodes,
            byte capacity,
            ref byte count);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_copy_vars(
            [Out] NativeVar[] vars,
            byte capacity,
            ref byte count);

        [DllImport("micronet_bridge", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mnviz_copy_messages(
            [Out] NativeMessage[] messages,
            byte capacity,
            ref byte count);
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    private struct NativeInitConfig
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string NodeName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string StunHost;
        public ushort StunPort;
        public ushort LocalPort;
        public uint HeartbeatMs;
        public uint OfflineTimeoutMs;
        public uint RetryIntervalMs;
        public byte RetryCount;
        public byte MaxNodes;
        public byte MaxVars;
        public byte MaxPending;
        public byte LogLevel;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public byte[] NodePrivkey;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeMetrics
    {
        public uint UptimeS;
        public uint FreeHeap;
        public byte ConnectedNodes;
        public byte GroupCount;
        public uint PacketsSent;
        public uint PacketsRecv;
        public uint Errors;
        public byte HealthScore;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeSnapshot
    {
        public byte NodeCount;
        public byte OnlineCount;
        public byte GroupCount;
        public byte VarCount;
        public byte MessageCount;
        public NativeMetrics LocalMetrics;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeNode
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public byte[] NodeId;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public byte[] InvitedBy;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
        public byte[] ExternalIp;
        public ushort ExternalPort;
        public uint FirstSeen;
        public uint LastSeen;
        public uint DbVersion;
        public byte GroupCount;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)]
        public byte[] GroupHashes;
        public uint PacketsSent;
        public uint PacketsRecv;
        public byte HealthScore;
        public uint FreeHeap;
        [MarshalAs(UnmanagedType.I1)]
        public bool IsOnline;
        [MarshalAs(UnmanagedType.I1)]
        public bool IsAuthorized;
        [MarshalAs(UnmanagedType.I1)]
        public bool IsSelf;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeVar
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public byte[] Key;
        public byte Type;
        public byte Access;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)]
        public byte[] Data;
        public nuint DataLen;
        public uint UpdatedAt;
        [MarshalAs(UnmanagedType.I1)]
        public bool IsPublic;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeMessage
    {
        public uint Timestamp;
        public byte Direction;
        public byte MessageType;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public byte[] Src;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public byte[] Dst;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)]
        public byte[] Payload;
        public uint PayloadLen;

        public bool SourceIsEmpty => Src.All(static b => b == 0);
        public bool DestinationIsEmpty => Dst.All(static b => b == 0);
    }
}

public sealed record BridgeMetrics(
    uint UptimeS,
    uint FreeHeap,
    byte ConnectedNodes,
    byte GroupCount,
    uint PacketsSent,
    uint PacketsRecv,
    uint Errors,
    byte HealthScore);

public sealed record BridgeNode(
    string DisplayName,
    string ShortId,
    bool IsOnline,
    bool IsSelf,
    int LastSeenAgeSeconds,
    byte GroupCount,
    bool IsAuthorized,
    IReadOnlyList<string> GroupHashes,
    uint PacketsSent,
    uint PacketsRecv,
    byte HealthScore,
    uint FreeHeap,
    byte[] NodeIdBytes);

public sealed record BridgeVar(
    string NodeName,
    string Key,
    string TypeName,
    string DisplayValue,
    int UpdatedAgeSeconds);

public sealed record BridgeMessage(
    DateTime Timestamp,
    string SourceName,
    string DestinationName,
    string Kind,
    string Preview,
    string Payload);

public sealed record BridgeSnapshot(
    byte NodeCount,
    byte OnlineCount,
    byte GroupCount,
    BridgeMetrics Metrics,
    IReadOnlyList<BridgeNode> Nodes,
    IReadOnlyList<BridgeVar> Vars,
    IReadOnlyList<BridgeMessage> Messages);
