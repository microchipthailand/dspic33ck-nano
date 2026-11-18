// Snapshot.cs - render the GUI to PNG files without touching the screen (Control.DrawToBitmap, off-screen window).
using System;
using System.Drawing;
using System.Reflection;
using System.Windows.Forms;
namespace NanoBootloader
{
    public static class Snapshot
    {
        [STAThread]
        public static int Main(string[] a)
        {
            Application.EnableVisualStyles();
            MainForm f = new MainForm();
            f.StartPosition = FormStartPosition.Manual;
            f.Location = new Point(-5000, -5000);
            f.ShowInTaskbar = false;
            BindingFlags bf = BindingFlags.NonPublic | BindingFlags.Instance;
            ((TextBox)typeof(MainForm).GetField("tbFile", bf).GetValue(f)).Text = a[0];
            f.Show();
            typeof(MainForm).GetMethod("LoadImage", bf).Invoke(f, new object[] { false });
            Application.DoEvents();
            TabControl tabs = (TabControl)typeof(MainForm).GetField("tabs", bf).GetValue(f);
            for (int i = 0; i < 2; i++)
            {
                tabs.SelectedIndex = i; Application.DoEvents();
                using (Bitmap b = new Bitmap(f.Width, f.Height)) { f.DrawToBitmap(b, new Rectangle(0, 0, f.Width, f.Height)); b.Save(a[1 + i]); }
            }
            f.Close();
            return 0;
        }
    }
}
