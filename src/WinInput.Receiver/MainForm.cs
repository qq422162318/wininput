using System;
using System.Drawing;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using WinInput.Shared;

namespace WinInput.Receiver
{
    public class MainForm : Form
    {
        TextBox address = new TextBox { Text = "0.0.0.0" }, port = new TextBox { Text = "18765" }, secret = new TextBox { UseSystemPasswordChar = true };
        Button start = new Button { Text = "启动" }; Label status = new Label { AutoSize = true, Text = "未启动" };
        TcpListener listener; CancellationTokenSource stop;
        public MainForm() { Text = "WinInput 接收端"; ClientSize = new Size(420, 180); var p = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(10), ColumnCount = 2, RowCount = 5 }; p.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90)); p.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); Add(p,"监听地址",address,0); Add(p,"端口",port,1); Add(p,"共享密钥",secret,2); p.Controls.Add(start,0,3); p.Controls.Add(status,1,3); Controls.Add(p); start.Click += Toggle; FormClosing += (s,e) => Stop(); }
        void Add(TableLayoutPanel p,string label,Control c,int row) { p.Controls.Add(new Label { Text=label, AutoSize=true },0,row); c.Dock=DockStyle.Fill; p.Controls.Add(c,1,row); }
        async void Toggle(object s, EventArgs e) { if (listener != null) { Stop(); return; } if (secret.Text.Length < 16) { MessageBox.Show("共享密钥至少16个字符"); return; } IPAddress ip; int n; if(!IPAddress.TryParse(address.Text,out ip)||!int.TryParse(port.Text,out n)||n<1||n>65535){MessageBox.Show("地址或端口无效");return;} listener=new TcpListener(ip,n); stop=new CancellationTokenSource(); listener.Start(); start.Text="停止"; status.Text="监听中"; try { while(true) { var client=await listener.AcceptTcpClientAsync(); _=Task.Run(()=>Handle(client,stop.Token)); } } catch (ObjectDisposedException) { } catch (Exception ex) { if(listener!=null) MessageBox.Show(ex.Message); } }
        void Handle(TcpClient client,CancellationToken token) { using(client) using(var stream=client.GetStream()) try { while(!token.IsCancellationRequested) { var m=Protocol.Read(stream); if(m==null||m.Length<3||m[0]!=secret.Text) return; if(m[1]=="text") Input.Text(m[2]); else if(m[1]=="key") Input.Key(m[2]); } } catch { } }
        void Stop() { stop?.Cancel(); listener?.Stop(); listener=null; if(!IsDisposed){start.Text="启动";status.Text="已停止";} }
    }
    static class Input
    {
        [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, INPUT[] inputs, int size);
        const uint KEYEVENTF_KEYUP=2; static ushort Vk(string s) { s=s.ToUpperInvariant(); if(s.Length==1&&s[0]>='A'&&s[0]<='Z')return (ushort)s[0]; if(s.StartsWith("F")&&int.TryParse(s.Substring(1),out var f)&&f>=1&&f<=12)return (ushort)(0x70+f-1); return s=="SHIFT"?(ushort)0x10:s=="CTRL"?(ushort)0x11:s=="ALT"?(ushort)0x12:(ushort)0; }
        public static void Text(string text) { var a=new INPUT[text.Length*2]; for(int i=0;i<text.Length;i++) { a[i*2]=K(0,4,(ushort)text[i]); a[i*2+1]=K(0,4|KEYEVENTF_KEYUP,(ushort)text[i]); } if(a.Length>0)SendInput((uint)a.Length,a,Marshal.SizeOf(typeof(INPUT))); }
        public static void Key(string value) { var x=value.Split('+'); var keys=new ushort[x.Length]; for(int i=0;i<x.Length;i++){keys[i]=Vk(x[i]);if(keys[i]==0)return;} var a=new INPUT[keys.Length*2];for(int i=0;i<keys.Length;i++)a[i]=K(keys[i],0);for(int i=keys.Length-1;i>=0;i--)a[keys.Length+(keys.Length-1-i)]=K(keys[i],KEYEVENTF_KEYUP);SendInput((uint)a.Length,a,Marshal.SizeOf(typeof(INPUT))); }
        static INPUT K(ushort v,uint f,ushort scan=0){return new INPUT{type=1,u=new U{ki=new KEYBDINPUT{wVk=v,wScan=scan,dwFlags=f}}};}
        [StructLayout(LayoutKind.Sequential)] struct INPUT{public uint type;public U u;} [StructLayout(LayoutKind.Explicit)] struct U{[FieldOffset(0)]public KEYBDINPUT ki;} [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT{public ushort wVk,wScan;public uint dwFlags,time;public IntPtr dwExtraInfo;}
    }
}