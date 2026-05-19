using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Media;

namespace MicronetViz;

public sealed class SummaryCardViewModel : ObservableObject
{
    private string _value;
    private string _hint;

    public string Label { get; }

    public string Value
    {
        get => _value;
        set => SetProperty(ref _value, value);
    }

    public string Hint
    {
        get => _hint;
        set => SetProperty(ref _hint, value);
    }

    public SummaryCardViewModel(string label, string value, string hint)
    {
        Label = label;
        _value = value;
        _hint = hint;
    }
}

public sealed class MessageViewModel
{
    public required DateTime Timestamp { get; init; }
    public required string Source { get; init; }
    public required string Destination { get; init; }
    public required string Kind { get; init; }
    public required string Preview { get; init; }
    public required string Payload { get; init; }

    public string Route => $"{Source} -> {Destination}";
    public string TimestampText => Timestamp.ToString("HH:mm:ss");
}

public sealed class DatabaseEntryViewModel : ObservableObject
{
    private string _value = string.Empty;
    private DateTime _updatedAt;

    public required string NodeName { get; init; }
    public required string Key { get; init; }
    public required string Type { get; init; }

    public string Value
    {
        get => _value;
        set => SetProperty(ref _value, value);
    }

    public DateTime UpdatedAt
    {
        get => _updatedAt;
        set
        {
            if (SetProperty(ref _updatedAt, value))
            {
                OnPropertyChanged(nameof(UpdatedAgo));
            }
        }
    }

    public string UpdatedAgo => $"{Math.Max(0, (int)(DateTime.Now - UpdatedAt).TotalSeconds)} s";
}

public sealed class RegisteredDeviceViewModel : ObservableObject
{
    private string _alias = string.Empty;
    private string _nodeIdHex = string.Empty;

    public string Alias
    {
        get => _alias;
        set => SetProperty(ref _alias, value);
    }

    public string NodeIdHex
    {
        get => _nodeIdHex;
        set => SetProperty(ref _nodeIdHex, value);
    }
}

public sealed class NodeViewModel : ObservableObject
{
    private bool _isOnline;
    private int _packetsSent;
    private int _packetsReceived;
    private int _healthScore;
    private int _freeHeapKb;
    private DateTime _lastSeen;
    private int _messageRate;
    private bool _isSelf;

    public required string Name { get; init; }
    public required string NodeId { get; init; }
    public required string Role { get; init; }
    public required Brush AccentBrush { get; init; }
    public required byte[] NodeIdBytes { get; init; }
    public ObservableCollection<string> Peers { get; } = [];

    public string ShortLabel => string.Concat(Name.Split(' ', StringSplitOptions.RemoveEmptyEntries).Select(part => part[0])).ToUpperInvariant();

    public bool IsOnline
    {
        get => _isOnline;
        set
        {
            if (SetProperty(ref _isOnline, value))
            {
                OnPropertyChanged(nameof(OnlineText));
                OnPropertyChanged(nameof(StatusBrush));
            }
        }
    }

    public int PacketsSent
    {
        get => _packetsSent;
        set => SetProperty(ref _packetsSent, value);
    }

    public int PacketsReceived
    {
        get => _packetsReceived;
        set => SetProperty(ref _packetsReceived, value);
    }

    public int HealthScore
    {
        get => _healthScore;
        set => SetProperty(ref _healthScore, value);
    }

    public int FreeHeapKb
    {
        get => _freeHeapKb;
        set
        {
            if (SetProperty(ref _freeHeapKb, value))
            {
                OnPropertyChanged(nameof(FreeHeapText));
            }
        }
    }

    public DateTime LastSeen
    {
        get => _lastSeen;
        set
        {
            if (SetProperty(ref _lastSeen, value))
            {
                OnPropertyChanged(nameof(LastSeenText));
            }
        }
    }

    public int MessageRate
    {
        get => _messageRate;
        set
        {
            if (SetProperty(ref _messageRate, value))
            {
                OnPropertyChanged(nameof(MessageRateText));
            }
        }
    }

    public bool IsSelf
    {
        get => _isSelf;
        set => SetProperty(ref _isSelf, value);
    }

    public string OnlineText => IsOnline ? "ONLINE" : "OFFLINE";
    public Brush StatusBrush => IsOnline ? Brushes.LightGreen : Brushes.IndianRed;
    public string LastSeenText => IsOnline ? "aktivny teraz" : $"seen {(int)(DateTime.Now - LastSeen).TotalSeconds}s";
    public string MessageRateText => $"{MessageRate} msg/min";
    public string FreeHeapText => $"{FreeHeapKb:N0} KB";
}

public sealed class RelayCommand : ICommand
{
    private readonly Action<object?> _execute;
    private readonly Predicate<object?>? _canExecute;

    public RelayCommand(Action<object?> execute, Predicate<object?>? canExecute = null)
    {
        _execute = execute;
        _canExecute = canExecute;
    }

    public event EventHandler? CanExecuteChanged;

    public bool CanExecute(object? parameter)
    {
        return _canExecute?.Invoke(parameter) ?? true;
    }

    public void Execute(object? parameter)
    {
        _execute(parameter);
    }

    public void RaiseCanExecuteChanged()
    {
        CanExecuteChanged?.Invoke(this, EventArgs.Empty);
    }
}

public abstract class ObservableObject : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }
}

public sealed class InverseBooleanToVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, System.Globalization.CultureInfo culture)
    {
        var boolValue = value is bool flag && flag;
        return boolValue ? System.Windows.Visibility.Collapsed : System.Windows.Visibility.Visible;
    }

    public object ConvertBack(object value, Type targetType, object parameter, System.Globalization.CultureInfo culture)
    {
        throw new NotSupportedException();
    }
}
