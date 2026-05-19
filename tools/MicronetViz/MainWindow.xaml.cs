using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace MicronetViz;

public partial class MainWindow : Window
{
    private bool _isFormattingManualTarget;
    private bool _isFormattingGroupHash;
    private bool _isFormattingGroupKey;

    public MainWindow()
    {
        InitializeComponent();
        DataContext = new MainViewModel();
    }

    private static bool IsHexChar(char ch)
    {
        return (ch >= '0' && ch <= '9')
            || (ch >= 'a' && ch <= 'f')
            || (ch >= 'A' && ch <= 'F');
    }

    private static bool IsHexNodeChar(char ch)
    {
        return IsHexChar(ch) || ch == ':' || ch == '-';
    }

    private static bool IsHexNodeText(string text)
    {
        return text.All(IsHexNodeChar);
    }

    private static bool IsPlainHexText(string text)
    {
        return text.All(IsHexChar);
    }

    private void HexNodePreviewTextInput(object sender, TextCompositionEventArgs e)
    {
        e.Handled = !IsHexNodeText(e.Text);
    }

    private void HexPlainPreviewTextInput(object sender, TextCompositionEventArgs e)
    {
        e.Handled = !IsPlainHexText(e.Text);
    }

    private void HexNodePasting(object sender, DataObjectPastingEventArgs e)
    {
        if (!e.DataObject.GetDataPresent(DataFormats.Text))
        {
            e.CancelCommand();
            return;
        }

        var text = e.DataObject.GetData(DataFormats.Text) as string ?? string.Empty;
        if (!IsHexNodeText(text))
        {
            e.CancelCommand();
        }
    }

    private void HexPlainPasting(object sender, DataObjectPastingEventArgs e)
    {
        if (!e.DataObject.GetDataPresent(DataFormats.Text))
        {
            e.CancelCommand();
            return;
        }

        var text = e.DataObject.GetData(DataFormats.Text) as string ?? string.Empty;
        if (!IsPlainHexText(text))
        {
            e.CancelCommand();
        }
    }

    private static string FormatNodeIdText(string text, out int hexCount)
    {
        var hexChars = text.Where(IsHexChar).Take(64).ToArray();
        hexCount = hexChars.Length;

        if (hexChars.Length == 0)
        {
            return string.Empty;
        }

        var parts = new List<string>(hexChars.Length / 2 + 1);
        for (var i = 0; i < hexChars.Length; i += 2)
        {
            var partLen = Math.Min(2, hexChars.Length - i);
            parts.Add(new string(hexChars, i, partLen).ToLowerInvariant());
        }

        return string.Join(':', parts);
    }

    private static string FormatCompactHexText(string text, int maxHexChars, out int hexCount)
    {
        var hexChars = text.Where(IsHexChar).Take(maxHexChars).ToArray();
        hexCount = hexChars.Length;
        return new string(hexChars).ToLowerInvariant();
    }

    private static int CountHexBeforeCaret(string text, int caretIndex)
    {
        var safeIndex = Math.Clamp(caretIndex, 0, text.Length);
        var count = 0;
        for (var i = 0; i < safeIndex; i++)
        {
            if (IsHexChar(text[i]))
            {
                count++;
            }
        }
        return Math.Min(count, 64);
    }

    private static int CaretIndexFromHexCount(string formattedText, int desiredHexCount)
    {
        if (desiredHexCount <= 0)
        {
            return 0;
        }

        var seenHex = 0;
        for (var i = 0; i < formattedText.Length; i++)
        {
            if (IsHexChar(formattedText[i]))
            {
                seenHex++;
                if (seenHex >= desiredHexCount)
                {
                    return Math.Min(i + 1, formattedText.Length);
                }
            }
        }

        return formattedText.Length;
    }

    private void ManualTargetNodeIdTextChanged(object sender, TextChangedEventArgs e)
    {
        if (_isFormattingManualTarget || sender is not TextBox textBox)
        {
            return;
        }

        var caretHexCount = CountHexBeforeCaret(textBox.Text, textBox.CaretIndex);
        var formatted = FormatNodeIdText(textBox.Text, out _);
        if (formatted == textBox.Text)
        {
            return;
        }

        _isFormattingManualTarget = true;
        textBox.Text = formatted;
        textBox.CaretIndex = CaretIndexFromHexCount(formatted, caretHexCount);
        _isFormattingManualTarget = false;
    }

    private void GroupHashTextChanged(object sender, TextChangedEventArgs e)
    {
        if (_isFormattingGroupHash || sender is not TextBox textBox)
        {
            return;
        }

        var caretHexCount = CountHexBeforeCaret(textBox.Text, textBox.CaretIndex);
        var formatted = FormatCompactHexText(textBox.Text, 32, out _);
        if (formatted == textBox.Text)
        {
            return;
        }

        _isFormattingGroupHash = true;
        textBox.Text = formatted;
        textBox.CaretIndex = Math.Min(caretHexCount, textBox.Text.Length);
        _isFormattingGroupHash = false;
    }

    private void GroupKeyTextChanged(object sender, TextChangedEventArgs e)
    {
        if (_isFormattingGroupKey || sender is not TextBox textBox)
        {
            return;
        }

        var caretHexCount = CountHexBeforeCaret(textBox.Text, textBox.CaretIndex);
        var formatted = FormatCompactHexText(textBox.Text, 32, out _);
        if (formatted == textBox.Text)
        {
            return;
        }

        _isFormattingGroupKey = true;
        textBox.Text = formatted;
        textBox.CaretIndex = Math.Min(caretHexCount, textBox.Text.Length);
        _isFormattingGroupKey = false;
    }

    private void ExitButton_Click(object sender, RoutedEventArgs e)
    {
        Close();
    }

    private void MinimizeButton_Click(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState.Minimized;
    }

    private void MaximizeButton_Click(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    }

    private void HeaderBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount == 2)
        {
            WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
            return;
        }

        DragMove();
    }
}
