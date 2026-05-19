using System.Collections.ObjectModel;
using System.Windows.Media;
using System.Windows.Input;
using System.Windows.Threading;

using System.IO;
using System.Text.Json;

namespace MicronetViz;

public sealed class MainViewModel : ObservableObject
{
    private readonly DispatcherTimer _timer;
    private readonly MicronetBridgeClient _bridge = new();
    private bool _usingBridge;
    private string _statusText = "Inicializácia";
    private int _tickCount;
    private MessageViewModel? _selectedMessage;
    private NodeViewModel _selectedNode;
    private string _publishKey = "viz.note";
    private string _publishValue = "hello from wpf";
    private string _customPayload = "ping";
    private byte _customMsgType = 32;
    private string _manualTargetNodeId = "";
    private string _groupHash = "";
    private string _groupKey = "";
    private string _appNodeIdHex = "cakam na bridge snapshot";
    private string _registerAliasInput = "";
    private string _registerNodeIdInput = "";
    private RegisteredDeviceViewModel? _selectedRegisteredDevice;

    public ObservableCollection<SummaryCardViewModel> SummaryCards { get; } = [];
    public ObservableCollection<NodeViewModel> Nodes { get; } = [];
    public ObservableCollection<MessageViewModel> Messages { get; } = [];
    public ObservableCollection<DatabaseEntryViewModel> DatabaseEntries { get; } = [];
    public ObservableCollection<NodeViewModel> SendTargets { get; } = [];
    public ObservableCollection<RegisteredDeviceViewModel> RegisteredDevices { get; } = [];

    public MainViewModel()
    {
        SeedSummaryCards();
        LoadRegisteredDevices();

        _usingBridge = _bridge.TryInitialize(out var bridgeStatus);
        if (_usingBridge)
        {
            StatusText = bridgeStatus;
            PullBridgeState();
        }
        else
        {
            StatusText = $"{bridgeStatus} - bez live dát";
            SeedSimulation();
        }

        _selectedNode = Nodes.FirstOrDefault() ?? CreatePlaceholderNode();
        _selectedMessage = Messages.FirstOrDefault();

        _timer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(1.5),
        };
        _timer.Tick += (_, _) =>
        {
            TickCount++;
            if (_usingBridge)
            {
                if (!_bridge.Tick())
                {
                    _usingBridge = false;
                    StatusText = "Bridge stratil spojenie - bez live dát";
                    OnPropertyChanged(nameof(MeshHint));
                    OnPropertyChanged(nameof(DatabaseHint));
                    RaiseCommandStates();
                    if (Nodes.Count == 0)
                    {
                        SeedSimulation();
                    }
                }
                else
                {
                    PullBridgeState();
                    return;
                }
            }

            AdvanceSimulation();
        };
        _timer.Start();

        PublishCommand = new RelayCommand(_ => ExecutePublish(), _ => _usingBridge);
        UpdateCommand = new RelayCommand(_ => ExecuteUpdate(), _ => _usingBridge);
        SendCustomCommand = new RelayCommand(_ => ExecuteSendCustom(), _ => _usingBridge && HasResolvedTarget && !string.IsNullOrWhiteSpace(CustomPayload));
        BroadcastCommand = new RelayCommand(_ => ExecuteBroadcastCustom(), _ => _usingBridge && IsGroupHashValid && !string.IsNullOrWhiteSpace(CustomPayload));
        GroupCreateCommand = new RelayCommand(_ => ExecuteGroupCreate(), _ => _usingBridge);
        GroupJoinCommand = new RelayCommand(_ => ExecuteGroupJoin(), _ => _usingBridge && IsGroupHashValid && IsGroupKeyValid);
        GroupLeaveCommand = new RelayCommand(_ => ExecuteGroupLeave(), _ => _usingBridge && IsGroupHashValid);
        ArduinoPingCommand = new RelayCommand(_ => ExecuteArduinoQuickCommand("ping"), _ => _usingBridge && HasResolvedTarget);
        ArduinoLedToggleCommand = new RelayCommand(_ => ExecuteArduinoQuickCommand("led:toggle"), _ => _usingBridge && HasResolvedTarget);
        ArduinoRelayToggleCommand = new RelayCommand(_ => ExecuteArduinoQuickCommand("relay:toggle"), _ => _usingBridge && HasResolvedTarget);
        RegisterDeviceCommand = new RelayCommand(_ => ExecuteRegisterDevice(), _ => CanRegisterDevice);
        RemoveRegisteredDeviceCommand = new RelayCommand(_ => ExecuteRemoveRegisteredDevice(), _ => SelectedRegisteredDevice is not null);
        UseRegisteredDeviceCommand = new RelayCommand(_ => ExecuteUseRegisteredDevice(), _ => SelectedRegisteredDevice is not null);
        RaiseCommandStates();
    }

    public string StatusText
    {
        get => _statusText;
        private set => SetProperty(ref _statusText, value);
    }
    public string MeshHint => _usingBridge
        ? "Snapshot z natívneho micronet_bridge.dll"
        : "Bridge offline - žiadne live uzly";
    public string DatabaseHint => _usingBridge
        ? "Lokálny snapshot microdb premenných cez C bridge"
        : "Bridge offline - žiadne live microdb dáta";

    public int TickCount
    {
        get => _tickCount;
        private set => SetProperty(ref _tickCount, value);
    }

    public MessageViewModel? SelectedMessage
    {
        get => _selectedMessage;
        set
        {
            if (SetProperty(ref _selectedMessage, value))
            {
                OnPropertyChanged(nameof(HasSelectedMessage));
            }
        }
    }

    public bool HasSelectedMessage => SelectedMessage is not null;

    public NodeViewModel SelectedNode
    {
        get => _selectedNode;
        set => SetProperty(ref _selectedNode, value);
    }

    public string PublishKey
    {
        get => _publishKey;
        set => SetProperty(ref _publishKey, value);
    }

    public string PublishValue
    {
        get => _publishValue;
        set => SetProperty(ref _publishValue, value);
    }

    public string CustomPayload
    {
        get => _customPayload;
        set
        {
            if (SetProperty(ref _customPayload, value))
            {
                RaiseCommandStates();
            }
        }
    }

    public byte CustomMsgType
    {
        get => _customMsgType;
        set => SetProperty(ref _customMsgType, value);
    }

    public string ManualTargetNodeId
    {
        get => _manualTargetNodeId;
        set
        {
            if (SetProperty(ref _manualTargetNodeId, value))
            {
                OnPropertyChanged(nameof(IsManualTargetValid));
                OnPropertyChanged(nameof(HasResolvedTarget));
                OnPropertyChanged(nameof(TargetValidationHint));
                OnPropertyChanged(nameof(TargetBorderBrush));
                RaiseCommandStates();
            }
        }
    }

    public string GroupHash
    {
        get => _groupHash;
        set
        {
            if (SetProperty(ref _groupHash, value))
            {
                OnPropertyChanged(nameof(IsGroupHashValid));
                OnPropertyChanged(nameof(GroupHashValidationHint));
                OnPropertyChanged(nameof(GroupHashBorderBrush));
                RaiseCommandStates();
            }
        }
    }

    public string GroupKey
    {
        get => _groupKey;
        set
        {
            if (SetProperty(ref _groupKey, value))
            {
                OnPropertyChanged(nameof(IsGroupKeyValid));
                OnPropertyChanged(nameof(GroupKeyValidationHint));
                OnPropertyChanged(nameof(GroupKeyBorderBrush));
                RaiseCommandStates();
            }
        }
    }

    public string RegisterAliasInput
    {
        get => _registerAliasInput;
        set
        {
            if (SetProperty(ref _registerAliasInput, value))
            {
                OnPropertyChanged(nameof(CanRegisterDevice));
                RaiseCommandStates();
            }
        }
    }

    public string RegisterNodeIdInput
    {
        get => _registerNodeIdInput;
        set
        {
            if (SetProperty(ref _registerNodeIdInput, value))
            {
                OnPropertyChanged(nameof(CanRegisterDevice));
                RaiseCommandStates();
            }
        }
    }

    private NodeViewModel? _selectedTargetNode;
    public NodeViewModel? SelectedTargetNode
    {
        get => _selectedTargetNode;
        set
        {
            if (SetProperty(ref _selectedTargetNode, value))
            {
                OnPropertyChanged(nameof(HasResolvedTarget));
                OnPropertyChanged(nameof(TargetValidationHint));
                OnPropertyChanged(nameof(TargetBorderBrush));
                RaiseCommandStates();
            }
        }
    }

    public RegisteredDeviceViewModel? SelectedRegisteredDevice
    {
        get => _selectedRegisteredDevice;
        set
        {
            if (SetProperty(ref _selectedRegisteredDevice, value))
            {
                RaiseCommandStates();
            }
        }
    }

    public ICommand PublishCommand { get; }
    public ICommand UpdateCommand { get; }
    public ICommand SendCustomCommand { get; }
    public ICommand BroadcastCommand { get; }
    public ICommand GroupCreateCommand { get; }
    public ICommand GroupJoinCommand { get; }
    public ICommand GroupLeaveCommand { get; }
    public ICommand ArduinoPingCommand { get; }
    public ICommand ArduinoLedToggleCommand { get; }
    public ICommand ArduinoRelayToggleCommand { get; }
    public ICommand RegisterDeviceCommand { get; }
    public ICommand RemoveRegisteredDeviceCommand { get; }
    public ICommand UseRegisteredDeviceCommand { get; }
    public string AppListenPort => "33477";
    public string AppNodeIdHex
    {
        get => _appNodeIdHex;
        private set => SetProperty(ref _appNodeIdHex, value);
    }
    public bool IsManualTargetValid => string.IsNullOrWhiteSpace(ManualTargetNodeId) || TryParseHexBytes(ManualTargetNodeId, 32, out _);
    public bool HasResolvedTarget => (!string.IsNullOrWhiteSpace(ManualTargetNodeId) && IsManualTargetValid) || SelectedTargetNode is not null;
    public bool IsGroupHashValid => TryParseHexBytes(GroupHash, 16, out _);
    public bool IsGroupKeyValid => TryParseHexBytes(GroupKey, 16, out _);
    public string TargetValidationHint => HasResolvedTarget ? "target OK" : "target: vyber uzol alebo zadaj 64 hex";
    public string GroupHashValidationHint => IsGroupHashValid ? "group hash OK" : "group hash: 32 hex";
    public string GroupKeyValidationHint => IsGroupKeyValid ? "group key OK" : "group key: 32 hex";
    public Brush TargetBorderBrush => HasResolvedTarget ? BrushFromHex("#46B96A") : BrushFromHex("#C25555");
    public Brush GroupHashBorderBrush => IsGroupHashValid ? BrushFromHex("#46B96A") : BrushFromHex("#C25555");
    public Brush GroupKeyBorderBrush => IsGroupKeyValid ? BrushFromHex("#46B96A") : BrushFromHex("#C25555");
    public bool CanRegisterDevice => !string.IsNullOrWhiteSpace(RegisterAliasInput)
        && TryParseHexBytes(RegisterNodeIdInput, 32, out _);

    private void SeedSummaryCards()
    {
        SummaryCards.Add(new SummaryCardViewModel("Uzly", "0", "bez dát"));
        SummaryCards.Add(new SummaryCardViewModel("Skupiny", "0", "bez dát"));
        SummaryCards.Add(new SummaryCardViewModel("Správy", "0", "bez dát"));
        SummaryCards.Add(new SummaryCardViewModel("MicroDB", "0", "bez dát"));
        SummaryCards.Add(new SummaryCardViewModel("Štatistiky", "0", "bez dát"));
        SummaryCards.Add(new SummaryCardViewModel("Health avg", "0", "bez dát"));
    }

    private void PullBridgeState()
    {
        var snapshot = _bridge.GetSnapshot();
        if (snapshot is null)
        {
            return;
        }

        ReplaceNodes(snapshot.Nodes);
        ReplaceDatabase(snapshot.Vars);
        ReplaceMessages(snapshot.Messages);

        SummaryCards[0].Value = snapshot.NodeCount.ToString();
        SummaryCards[0].Hint = $"{snapshot.OnlineCount} online";
        SummaryCards[1].Value = snapshot.GroupCount.ToString();
        SummaryCards[1].Hint = "aktívne skupiny";
        SummaryCards[2].Value = snapshot.Messages.Count.ToString();
        SummaryCards[2].Hint = "event buffer";
        SummaryCards[3].Value = snapshot.Vars.Count.ToString();
        SummaryCards[3].Hint = "lokálne kľúče";
        SummaryCards[4].Value = $"{snapshot.Metrics.PacketsSent + snapshot.Metrics.PacketsRecv}";
        SummaryCards[4].Hint = $"tx {snapshot.Metrics.PacketsSent} / rx {snapshot.Metrics.PacketsRecv}";
        SummaryCards[5].Value = snapshot.Metrics.HealthScore.ToString();
        SummaryCards[5].Hint = $"heap {snapshot.Metrics.FreeHeap / 1024} KB";

        SelectedNode = Nodes.FirstOrDefault(node => node.IsOnline) ?? Nodes.FirstOrDefault() ?? CreatePlaceholderNode();
        SelectedMessage = Messages.FirstOrDefault();
    }

    private void ReplaceNodes(IReadOnlyList<BridgeNode> nodes)
    {
        Nodes.Clear();
        foreach (var node in nodes)
        {
            var vm = new NodeViewModel
            {
                Name = node.DisplayName,
                NodeId = node.ShortId,
                Role = node.IsSelf ? "lokálny WPF host" : "remote node",
                AccentBrush = BrushFromHex(node.IsSelf ? "#7EE787" : "#69C3FF"),
                IsOnline = node.IsOnline,
                HealthScore = node.HealthScore,
                PacketsSent = (int)node.PacketsSent,
                PacketsReceived = (int)node.PacketsRecv,
                FreeHeapKb = (int)(node.FreeHeap / 1024U),
                LastSeen = DateTime.Now.AddSeconds(-node.LastSeenAgeSeconds),
                MessageRate = Math.Max(1, Messages.Count(message => message.Source == node.DisplayName)),
                IsSelf = node.IsSelf,
                NodeIdBytes = node.NodeIdBytes,
            };
            Nodes.Add(vm);
        }

        SendTargets.Clear();
        foreach (var target in Nodes.Where(node => !node.IsSelf))
        {
            SendTargets.Add(target);
        }
        if (SelectedTargetNode is null || !SendTargets.Contains(SelectedTargetNode))
        {
            SelectedTargetNode = SendTargets.FirstOrDefault();
        }

        var self = nodes.FirstOrDefault(node => node.IsSelf);
        AppNodeIdHex = self is null ? "self node id nie je dostupny" : ToHex(self.NodeIdBytes);
        RaiseCommandStates();
    }

    private void ReplaceDatabase(IReadOnlyList<BridgeVar> vars)
    {
        DatabaseEntries.Clear();
        foreach (var item in vars)
        {
            DatabaseEntries.Add(new DatabaseEntryViewModel
            {
                NodeName = item.NodeName,
                Key = item.Key,
                Type = item.TypeName,
                Value = item.DisplayValue,
                UpdatedAt = DateTime.Now.AddSeconds(-item.UpdatedAgeSeconds),
            });
        }
    }

    private void ReplaceMessages(IReadOnlyList<BridgeMessage> messages)
    {
        Messages.Clear();
        foreach (var item in messages)
        {
            Messages.Add(new MessageViewModel
            {
                Timestamp = item.Timestamp,
                Source = item.SourceName,
                Destination = item.DestinationName,
                Kind = item.Kind,
                Preview = item.Preview,
                Payload = item.Payload,
            });
        }
    }

    private void SeedSimulation()
    {
        SetNoDataState();
    }

    private void AdvanceSimulation()
    {
        SetNoDataState();
    }

    private void SetNoDataState()
    {
        Nodes.Clear();
        DatabaseEntries.Clear();
        Messages.Clear();
        SendTargets.Clear();
        SelectedTargetNode = null;
        SelectedNode = CreatePlaceholderNode();
        SelectedMessage = null;
        AppNodeIdHex = "bridge offline";

        SummaryCards[0].Value = "0";
        SummaryCards[0].Hint = "offline";
        SummaryCards[1].Value = "0";
        SummaryCards[1].Hint = "offline";
        SummaryCards[2].Value = "0";
        SummaryCards[2].Hint = "offline";
        SummaryCards[3].Value = "0";
        SummaryCards[3].Hint = "offline";
        SummaryCards[4].Value = "0";
        SummaryCards[4].Hint = "offline";
        SummaryCards[5].Value = "0";
        SummaryCards[5].Hint = "offline";
    }

    private void ExecutePublish()
    {
        if (!_usingBridge || string.IsNullOrWhiteSpace(PublishKey))
        {
            return;
        }

        var err = _bridge.PublishText(PublishKey, PublishValue ?? string.Empty);
        StatusText = err == 0 ? "publish OK" : $"publish err={err}";
    }

    private void ExecuteUpdate()
    {
        if (!_usingBridge || string.IsNullOrWhiteSpace(PublishKey))
        {
            return;
        }

        var err = _bridge.UpdateText(PublishKey, PublishValue ?? string.Empty);
        StatusText = err == 0 ? "update OK" : $"update err={err}";
    }

    private void ExecuteSendCustom()
    {
        if (!_usingBridge)
        {
            return;
        }

        if (!TryResolveTargetNodeId(out var target))
        {
            return;
        }

        var err = _bridge.SendCustom(target, CustomMsgType, CustomPayload ?? string.Empty);
        StatusText = err == 0 ? "send_custom OK" : $"send_custom err={err}";
    }

    private void ExecuteArduinoQuickCommand(string commandText)
    {
        if (!_usingBridge || string.IsNullOrWhiteSpace(commandText))
        {
            return;
        }
        if (!TryResolveTargetNodeId(out var target))
        {
            return;
        }

        var err = _bridge.SendCustom(target, 32, commandText);
        StatusText = err == 0 ? $"arduino cmd OK: {commandText}" : $"arduino cmd err={err}";
    }

    private void ExecuteRegisterDevice()
    {
        if (!TryParseHexBytes(RegisterNodeIdInput, 32, out var parsedNodeId))
        {
            StatusText = "register: node id format error";
            return;
        }

        var alias = RegisterAliasInput.Trim();
        if (string.IsNullOrWhiteSpace(alias))
        {
            StatusText = "register: alias missing";
            return;
        }

        var normalizedNodeId = ToHex(parsedNodeId);
        var existing = RegisteredDevices.FirstOrDefault(device =>
            string.Equals(device.NodeIdHex, normalizedNodeId, StringComparison.OrdinalIgnoreCase));

        if (existing is null)
        {
            existing = new RegisteredDeviceViewModel
            {
                Alias = alias,
                NodeIdHex = normalizedNodeId,
            };
            RegisteredDevices.Add(existing);
            StatusText = "register device OK";
        }
        else
        {
            existing.Alias = alias;
            StatusText = "register device updated";
        }

        SelectedRegisteredDevice = existing;
        RegisterAliasInput = string.Empty;
        RegisterNodeIdInput = string.Empty;
        SaveRegisteredDevices();
    }

    private void ExecuteRemoveRegisteredDevice()
    {
        if (SelectedRegisteredDevice is null)
        {
            return;
        }

        var alias = SelectedRegisteredDevice.Alias;
        RegisteredDevices.Remove(SelectedRegisteredDevice);
        SelectedRegisteredDevice = null;
        SaveRegisteredDevices();
        StatusText = $"device removed: {alias}";
    }

    private void ExecuteUseRegisteredDevice()
    {
        if (SelectedRegisteredDevice is null)
        {
            return;
        }

        ManualTargetNodeId = SelectedRegisteredDevice.NodeIdHex;
        StatusText = $"target set: {SelectedRegisteredDevice.Alias}";
    }

    private void ExecuteBroadcastCustom()
    {
        if (!_usingBridge)
        {
            return;
        }
        if (!TryParseHexBytes(GroupHash, 16, out var groupHash))
        {
            StatusText = "group hash format error";
            return;
        }

        var err = _bridge.BroadcastCustom(groupHash, CustomMsgType, CustomPayload ?? string.Empty);
        StatusText = err == 0 ? "broadcast_custom OK" : $"broadcast_custom err={err}";
    }

    private void ExecuteGroupCreate()
    {
        if (!_usingBridge)
        {
            return;
        }

        var err = _bridge.GroupCreate(out var hash, out var key);
        if (err == 0)
        {
            GroupHash = ToHex(hash);
            GroupKey = ToHex(key);
            StatusText = "group_create OK";
        }
        else
        {
            StatusText = $"group_create err={err}";
        }
    }

    private void ExecuteGroupJoin()
    {
        if (!_usingBridge)
        {
            return;
        }
        if (!TryParseHexBytes(GroupHash, 16, out var hash) || !TryParseHexBytes(GroupKey, 16, out var key))
        {
            StatusText = "group join input format error";
            return;
        }

        var err = _bridge.GroupJoin(hash, key);
        StatusText = err == 0 ? "group_join OK" : $"group_join err={err}";
    }

    private void ExecuteGroupLeave()
    {
        if (!_usingBridge)
        {
            return;
        }
        if (!TryParseHexBytes(GroupHash, 16, out var hash))
        {
            StatusText = "group hash format error";
            return;
        }

        var err = _bridge.GroupLeave(hash);
        StatusText = err == 0 ? "group_leave OK" : $"group_leave err={err}";
    }

    private void RaiseCommandStates()
    {
        (PublishCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (UpdateCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (SendCustomCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (BroadcastCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (GroupCreateCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (GroupJoinCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (GroupLeaveCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (ArduinoPingCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (ArduinoLedToggleCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (ArduinoRelayToggleCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (RegisterDeviceCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (RemoveRegisteredDeviceCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (UseRegisteredDeviceCommand as RelayCommand)?.RaiseCanExecuteChanged();
    }

    private bool TryResolveTargetNodeId(out byte[] target)
    {
        target = Array.Empty<byte>();

        if (!string.IsNullOrWhiteSpace(ManualTargetNodeId))
        {
            if (TryParseHexBytes(ManualTargetNodeId, 32, out var parsed))
            {
                target = parsed;
                return true;
            }

            StatusText = "target node id format error";
            return false;
        }

        if (SelectedTargetNode is not null && SelectedTargetNode.NodeIdBytes.Length == 32)
        {
            target = SelectedTargetNode.NodeIdBytes;
            return true;
        }

        StatusText = "target node id missing";
        return false;
    }

    private static bool TryParseHexBytes(string value, int expectedLen, out byte[] bytes)
    {
        bytes = Array.Empty<byte>();
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var normalized = value.Replace(":", "", StringComparison.Ordinal)
            .Replace("-", "", StringComparison.Ordinal)
            .Replace(" ", "", StringComparison.Ordinal)
            .Trim();

        if (normalized.Length != expectedLen * 2)
        {
            return false;
        }

        try
        {
            bytes = Convert.FromHexString(normalized);
            return bytes.Length == expectedLen;
        }
        catch
        {
            return false;
        }
    }

    private static string ToHex(byte[] bytes)
    {
        return string.Concat(bytes.Select(static b => b.ToString("x2")));
    }

    private static string RegisteredDevicesFilePath
    {
        get
        {
            var appData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            return Path.Combine(appData, "MicronetViz", "registered_devices.json");
        }
    }

    private void LoadRegisteredDevices()
    {
        try
        {
            var filePath = RegisteredDevicesFilePath;
            if (!File.Exists(filePath))
            {
                return;
            }

            var json = File.ReadAllText(filePath);
            var items = JsonSerializer.Deserialize<List<RegisteredDeviceRecord>>(json) ?? [];
            foreach (var item in items)
            {
                if (string.IsNullOrWhiteSpace(item.Alias) || !TryParseHexBytes(item.NodeIdHex, 32, out _))
                {
                    continue;
                }

                RegisteredDevices.Add(new RegisteredDeviceViewModel
                {
                    Alias = item.Alias.Trim(),
                    NodeIdHex = item.NodeIdHex.Trim().ToLowerInvariant(),
                });
            }
        }
        catch
        {
            // Ignore malformed or inaccessible file.
        }
    }

    private void SaveRegisteredDevices()
    {
        try
        {
            var filePath = RegisteredDevicesFilePath;
            var dir = Path.GetDirectoryName(filePath);
            if (!string.IsNullOrWhiteSpace(dir))
            {
                Directory.CreateDirectory(dir);
            }

            var items = RegisteredDevices
                .Select(static device => new RegisteredDeviceRecord(device.Alias, device.NodeIdHex))
                .ToList();
            var json = JsonSerializer.Serialize(items, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(filePath, json);
        }
        catch
        {
            // Keep in-memory list even if save fails.
        }
    }

    private sealed record RegisteredDeviceRecord(string Alias, string NodeIdHex);

    private static NodeViewModel CreatePlaceholderNode()
    {
        return new NodeViewModel
        {
            Name = "No Node",
            NodeId = "-",
            Role = "bez dát",
            AccentBrush = BrushFromHex("#7ECDFD"),
            IsOnline = false,
            HealthScore = 0,
            PacketsSent = 0,
            PacketsReceived = 0,
            FreeHeapKb = 0,
            LastSeen = DateTime.Now,
            MessageRate = 0,
            NodeIdBytes = new byte[32],
        };
    }

    private static SolidColorBrush BrushFromHex(string hex)
    {
        return (SolidColorBrush)new BrushConverter().ConvertFrom(hex)!;
    }
}

