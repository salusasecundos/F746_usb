using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Management;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Timers;
using System.Windows.Forms;
using System.Drawing;

namespace GenericHid
{
	///<summary>
	/// Project: GenericHid
	/// 
	/// ***********************************************************************
	/// Software License Agreement
	///
	/// Licensor grants any person obtaining a copy of this software ("You") 
	/// a worldwide, royalty-free, non-exclusive license, for the duration of 
	/// the copyright, free of charge, to store and execute the Software in a 
	/// computer system and to incorporate the Software or any portion of it 
	/// in computer programs You write.   
	/// 
	/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	/// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	/// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	/// THE SOFTWARE.
	/// ***********************************************************************
	/// 
	/// Author             
	/// Jan Axelson        
	/// 
	/// This software was written using Visual Studio Express 2012 for Windows
	/// Desktop building for the .NET Framework v4.5.
	/// 
	/// Purpose: 
	/// Demonstrates USB communications with a generic HID-class device
	/// 
	/// Requirements:
	/// Windows Vista or later and an attached USB generic Human Interface Device (HID).
	/// (Does not run on Windows XP or earlier because .NET Framework 4.5 will not install on these OSes.) 
	/// 
	/// Description:
	/// Finds an attached device that matches the vendor and product IDs in the form's 
	/// text boxes.
	/// 
	/// Retrieves the device's capabilities.
	/// Sends and requests HID reports.
	/// 
	/// Uses the System.Management class and Windows Management Instrumentation (WMI) to detect 
	/// when a device is attached or removed.
	/// 
	/// A list box displays the data sent and received along with error and status messages.
	/// You can select data to send and 1-time or periodic transfers.
	/// 
	/// You can change the size of the host's Input report buffer and request to use control
	/// transfers only to exchange Input and Output reports.
	/// 
	/// To view additional debugging messages, in the Visual Studio development environment,
	/// from the main menu, select Build > Configuration Manager > Active Solution Configuration 
	/// and select Configuration > Debug and from the main menu, select View > Output.
	/// 
	/// The application uses asynchronous FileStreams to read Input reports and write Output 
	/// reports so the application's main thread doesn't have to wait for the device to retrieve a 
	/// report when the HID driver's buffer is empty or send a report when the device's endpoint is busy. 
	/// 
	/// NOTE: the device is now a vendor-specific WinUSB Bulk interface, not a HID-class
	/// device; the paragraphs above describe the original Microsoft sample this project
	/// started from. The live communication path is FrmMain.RequestBulkExchange() via
	/// WinUsbDevice.cs. The legacy HID report-exchange code (FindTheHid and friends) and
	/// its supporting Hid.cs/HidDeclarations.cs/Debugging.cs/DebuggingDeclarations.cs were
	/// unreachable dead code and have been removed.
	///
	/// This project includes the following modules:
	///
	/// GenericHid.cs - runs the application.
	/// FrmMain.cs - routines specific to the form.
	/// WinUsbDevice.cs - WinUSB Bulk transport used to talk to the STM32 device.
	/// UsbInterfaceDiscovery.cs - locates the device's WinUSB interface GUID.
	/// DeviceManagement.cs - routine for obtaining a handle to a device from its GUID.
	/// FileIODeclarations.cs - Declarations for file-related API functions.
	/// DeviceManagementDeclarations.cs - Declarations for API functions used by DeviceManagement.cs.
	///
	/// Companion device firmware for several device CPUs is available from www.Lvr.com/hidpage.htm
	/// You can use any generic HID (not a system mouse or keyboard) that sends and receives reports.
	/// This application will not detect or communicate with non-HID-class devices.
	/// 
	/// For more information about HIDs and USB, and additional example device firmware to use
	/// with this application, visit Lakeview Research at http://Lvr.com 
	/// Send comments, bug reports, etc. to jan@Lvr.com or post on my PORTS forum: http://www.lvr.com/forum 
	/// 
	/// V6.2
	/// 11/12/13
	/// Disabled form buttons when a transfer is in progress.
	/// Other minor edits for clarity and readability.
	/// Will NOT run on Windows XP or earlier, see below.
	/// 
	/// V6.1
	/// 10/28/13
	/// Uses the .NET System.Management class to detect device arrival and removal with WMI instead of Win32 RegisterDeviceNotification.
	/// Other minor edits.
	/// Will NOT run on Windows XP or earlier, see below.
	///  
	/// V6.0
	/// 2/8/13
	/// This version will NOT run on Windows XP or earlier because the code uses .NET Framework 4.5 to support asynchronous FileStreams.
	/// The .NET Framework 4.5 redistributable is compatible with Windows 8, Windows 7 SP1, Windows Server 2008 R2 SP1, 
	/// Windows Server 2008 SP2, Windows Vista SP2, and Windows Vista SP3.
	/// For compatibility, replaced ToInt32 with ToInt64 here:
	/// IntPtr pDevicePathName = new IntPtr(detailDataBuffer.ToInt64() + 4);
	/// and here:
	/// if ((deviceNotificationHandle.ToInt64() == IntPtr.Zero.ToInt64()))
	/// For compatibility if the charset isn't English, added System.Globalization.CultureInfo.InvariantCulture here:
	/// if ((String.Compare(DeviceNameString, mydevicePathName, true, System.Globalization.CultureInfo.InvariantCulture) == 0))
	/// Replaced all Microsoft.VisualBasic namespace code with other .NET equivalents.
	/// Revised user interface for more flexibility.
	/// Moved interrupt-transfer and other HID-specific code to Hid.cs.
	/// Used JetBrains ReSharper to clean up the code: http://www.jetbrains.com/resharper/
	/// 
	/// V5.0
	/// 3/30/11
	/// Replaced ReadFile and WriteFile with FileStreams. Thanks to Joe Dunne and John on my Ports forum for tips on this.
	/// Simplified Hid.cs.
	/// Replaced the form timer with a system timer.
	/// 
	/// V4.6
	/// 1/12/10
	/// Supports Vendor IDs and Product IDs up to FFFFh.
	///
	/// V4.52
	/// 11/10/09
	/// Changed HIDD_ATTRIBUTES to use UInt16
	/// 
	/// V4.51
	/// 2/11/09
	/// Moved Free_ and similar to Finally blocks to ensure they execute.
	/// 
	/// V4.5
	/// 2/9/09
	/// Changes to support 64-bit systems, memory management, and other corrections. 
	/// Big thanks to Peter Nielsen.
	///  
	/// </summary>

	internal class FrmMain
		: Form
	{
		#region '"Windows Form Designer generated code "'
		public FrmMain()
		//: base()
		{
			// This call is required by the Windows Form Designer.
			InitializeComponent();
		}
		// Form overrides dispose to clean up the component list.
		protected override void Dispose(bool Disposing1)
		{
			if (Disposing1)
			{
				if (components != null)
				{
					components.Dispose();
				}
			}
			base.Dispose(Disposing1);
		}

		// Required by the Windows Form Designer
		private System.ComponentModel.IContainer components;
		public System.Windows.Forms.ToolTip ToolTip1;
        private ComboBox comboBox1;
        private NumericUpDown numericUpDown1;
        private TrackBar trackBar1;
        private TrackBar trackBar2;
        private TrackBar trackBar3;
        private GroupBox groupBox1;
        private Label label1;
        private Label label2;
        private Label label3;
        private NumericUpDown numericUpDown2;
        private NumericUpDown numericUpDown3;
        private NumericUpDown numericUpDown4;
        private NumericUpDown numericUpDown5;
        private NumericUpDown numericUpDown6;
        private NumericUpDown numericUpDown7;
        private NumericUpDown numericUpDown8;
        private NumericUpDown numericUpDown9;
        private NumericUpDown numericUpDown10;
        private System.Windows.Forms.DataVisualization.Charting.Chart chart1;
        private ProgressBar progressBar1;
        private TrackBar trackBar4;
        private Label label4;
        private TrackBar trackBar5;
        private Button button1;
        private System.Windows.Forms.Timer timer1;
        private Label label8;
        private Label label7;
        private Label label6;
        private Label label5;
        private Label label9;
        private Button btnMonitor;
        private GroupBox grpUsbStatus;
        private Label lblUsbIndicator;
        private Label lblUsbMessage;
        private Label lblUsbStatus;

		[System.Diagnostics.DebuggerStepThrough()]
		private void InitializeComponent()
		{
            this.components = new System.ComponentModel.Container();
            System.Windows.Forms.DataVisualization.Charting.ChartArea chartArea1 = new System.Windows.Forms.DataVisualization.Charting.ChartArea();
            System.Windows.Forms.DataVisualization.Charting.Legend legend1 = new System.Windows.Forms.DataVisualization.Charting.Legend();
            System.Windows.Forms.DataVisualization.Charting.Series series1 = new System.Windows.Forms.DataVisualization.Charting.Series();
            this.ToolTip1 = new System.Windows.Forms.ToolTip(this.components);
            this.btnMonitor = new System.Windows.Forms.Button();
            this.comboBox1 = new System.Windows.Forms.ComboBox();
            this.numericUpDown1 = new System.Windows.Forms.NumericUpDown();
            this.trackBar1 = new System.Windows.Forms.TrackBar();
            this.trackBar2 = new System.Windows.Forms.TrackBar();
            this.trackBar3 = new System.Windows.Forms.TrackBar();
            this.groupBox1 = new System.Windows.Forms.GroupBox();
            this.button1 = new System.Windows.Forms.Button();
            this.chart1 = new System.Windows.Forms.DataVisualization.Charting.Chart();
            this.label8 = new System.Windows.Forms.Label();
            this.label7 = new System.Windows.Forms.Label();
            this.label6 = new System.Windows.Forms.Label();
            this.label5 = new System.Windows.Forms.Label();
            this.label1 = new System.Windows.Forms.Label();
            this.label2 = new System.Windows.Forms.Label();
            this.label3 = new System.Windows.Forms.Label();
            this.numericUpDown2 = new System.Windows.Forms.NumericUpDown();
            this.numericUpDown3 = new System.Windows.Forms.NumericUpDown();
            this.numericUpDown4 = new System.Windows.Forms.NumericUpDown();
            this.numericUpDown5 = new System.Windows.Forms.NumericUpDown();
            this.numericUpDown6 = new System.Windows.Forms.NumericUpDown();
            this.numericUpDown7 = new System.Windows.Forms.NumericUpDown();
            this.numericUpDown8 = new System.Windows.Forms.NumericUpDown();
            this.numericUpDown9 = new System.Windows.Forms.NumericUpDown();
            this.numericUpDown10 = new System.Windows.Forms.NumericUpDown();
            this.progressBar1 = new System.Windows.Forms.ProgressBar();
            this.trackBar4 = new System.Windows.Forms.TrackBar();
            this.label4 = new System.Windows.Forms.Label();
            this.trackBar5 = new System.Windows.Forms.TrackBar();
            this.timer1 = new System.Windows.Forms.Timer(this.components);
            this.label9 = new System.Windows.Forms.Label();
            this.grpUsbStatus = new System.Windows.Forms.GroupBox();
            this.lblUsbIndicator = new System.Windows.Forms.Label();
            this.lblUsbMessage = new System.Windows.Forms.Label();
            this.lblUsbStatus = new System.Windows.Forms.Label();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown1)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.trackBar1)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.trackBar2)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.trackBar3)).BeginInit();
            this.groupBox1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.chart1)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown2)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown3)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown4)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown5)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown6)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown7)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown8)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown9)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown10)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.trackBar4)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.trackBar5)).BeginInit();
            this.grpUsbStatus.SuspendLayout();
            this.SuspendLayout();
            // 
            // btnMonitor
            // 
            this.btnMonitor.Location = new System.Drawing.Point(63, 157);
            this.btnMonitor.Name = "btnMonitor";
            this.btnMonitor.Size = new System.Drawing.Size(119, 22);
            this.btnMonitor.TabIndex = 64;
            this.btnMonitor.Text = "USB Monitor";
            this.btnMonitor.UseVisualStyleBackColor = true;
            this.btnMonitor.Click += new System.EventHandler(this.btnMonitor_Click);
            // 
            // comboBox1
            // 
            this.comboBox1.FormattingEnabled = true;
            this.comboBox1.Location = new System.Drawing.Point(31, 498);
            this.comboBox1.Name = "comboBox1";
            this.comboBox1.Size = new System.Drawing.Size(128, 22);
            this.comboBox1.TabIndex = 39;
            // 
            // numericUpDown1
            // 
            this.numericUpDown1.Location = new System.Drawing.Point(31, 238);
            this.numericUpDown1.Name = "numericUpDown1";
            this.numericUpDown1.Size = new System.Drawing.Size(128, 20);
            this.numericUpDown1.TabIndex = 40;
            // 
            // trackBar1
            // 
            this.trackBar1.Location = new System.Drawing.Point(187, 238);
            this.trackBar1.Maximum = 100;
            this.trackBar1.Name = "trackBar1";
            this.trackBar1.Size = new System.Drawing.Size(413, 45);
            this.trackBar1.TabIndex = 41;
            this.trackBar1.Scroll += new System.EventHandler(this.trackBar1_Scroll);
            this.trackBar1.MouseUp += new System.Windows.Forms.MouseEventHandler(this.TrackBar_Send_MouseUp);
            // 
            // trackBar2
            // 
            this.trackBar2.Location = new System.Drawing.Point(187, 316);
            this.trackBar2.Maximum = 100;
            this.trackBar2.Name = "trackBar2";
            this.trackBar2.Size = new System.Drawing.Size(413, 45);
            this.trackBar2.TabIndex = 42;
            this.trackBar2.Scroll += new System.EventHandler(this.trackBar2_Scroll);
            this.trackBar2.MouseUp += new System.Windows.Forms.MouseEventHandler(this.TrackBar_Send_MouseUp);
            // 
            // trackBar3
            // 
            this.trackBar3.Location = new System.Drawing.Point(187, 393);
            this.trackBar3.Maximum = 100;
            this.trackBar3.Name = "trackBar3";
            this.trackBar3.Size = new System.Drawing.Size(413, 45);
            this.trackBar3.TabIndex = 43;
            this.trackBar3.Scroll += new System.EventHandler(this.trackBar3_Scroll);
            this.trackBar3.MouseUp += new System.Windows.Forms.MouseEventHandler(this.TrackBar_Send_MouseUp);
            // 
            // groupBox1
            // 
            this.groupBox1.Controls.Add(this.button1);
            this.groupBox1.Controls.Add(this.chart1);
            this.groupBox1.Location = new System.Drawing.Point(659, 20);
            this.groupBox1.Name = "groupBox1";
            this.groupBox1.Size = new System.Drawing.Size(896, 779);
            this.groupBox1.TabIndex = 45;
            this.groupBox1.TabStop = false;
            this.groupBox1.Text = "Driver";
            // 
            // button1
            // 
            this.button1.Location = new System.Drawing.Point(6, 19);
            this.button1.Name = "button1";
            this.button1.Size = new System.Drawing.Size(75, 23);
            this.button1.TabIndex = 63;
            this.button1.Text = "Clear";
            this.button1.UseVisualStyleBackColor = true;
            this.button1.Click += new System.EventHandler(this.button1_Click);
            // 
            // chart1
            // 
            chartArea1.AxisX.ScaleView.SizeType = System.Windows.Forms.DataVisualization.Charting.DateTimeIntervalType.Seconds;
            chartArea1.AxisX.ScaleView.SmallScrollMinSizeType = System.Windows.Forms.DataVisualization.Charting.DateTimeIntervalType.Number;
            chartArea1.AxisX.ScaleView.SmallScrollSizeType = System.Windows.Forms.DataVisualization.Charting.DateTimeIntervalType.Hours;
            chartArea1.Name = "ChartArea1";
            this.chart1.ChartAreas.Add(chartArea1);
            legend1.Name = "Legend1";
            this.chart1.Legends.Add(legend1);
            this.chart1.Location = new System.Drawing.Point(6, 48);
            this.chart1.Name = "chart1";
            series1.ChartArea = "ChartArea1";
            series1.ChartType = System.Windows.Forms.DataVisualization.Charting.SeriesChartType.Spline;
            series1.Legend = "Legend1";
            series1.Name = "Series1";
            this.chart1.Series.Add(series1);
            this.chart1.Size = new System.Drawing.Size(884, 713);
            this.chart1.TabIndex = 62;
            this.chart1.Text = "chart1";
            // 
            // label8
            // 
            this.label8.AutoSize = true;
            this.label8.Font = new System.Drawing.Font("Arial", 30F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label8.Location = new System.Drawing.Point(388, 168);
            this.label8.Name = "label8";
            this.label8.Size = new System.Drawing.Size(124, 45);
            this.label8.TabIndex = 67;
            this.label8.Text = "label8";
            // 
            // label7
            // 
            this.label7.AutoSize = true;
            this.label7.Font = new System.Drawing.Font("Arial", 30F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label7.Location = new System.Drawing.Point(388, 117);
            this.label7.Name = "label7";
            this.label7.Size = new System.Drawing.Size(124, 45);
            this.label7.TabIndex = 66;
            this.label7.Text = "label7";
            // 
            // label6
            // 
            this.label6.AutoSize = true;
            this.label6.Font = new System.Drawing.Font("Arial", 30F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label6.Location = new System.Drawing.Point(388, 68);
            this.label6.Name = "label6";
            this.label6.Size = new System.Drawing.Size(124, 45);
            this.label6.TabIndex = 65;
            this.label6.Text = "label6";
            // 
            // label5
            // 
            this.label5.AutoSize = true;
            this.label5.Font = new System.Drawing.Font("Arial", 30F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label5.Location = new System.Drawing.Point(388, 17);
            this.label5.Name = "label5";
            this.label5.Size = new System.Drawing.Size(124, 45);
            this.label5.TabIndex = 64;
            this.label5.Text = "label5";
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Font = new System.Drawing.Font("Arial", 20F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label1.Location = new System.Drawing.Point(606, 244);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(30, 32);
            this.label1.TabIndex = 46;
            this.label1.Text = "0";
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Font = new System.Drawing.Font("Arial", 20F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label2.Location = new System.Drawing.Point(606, 322);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(30, 32);
            this.label2.TabIndex = 47;
            this.label2.Text = "0";
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Font = new System.Drawing.Font("Arial", 20F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label3.Location = new System.Drawing.Point(606, 401);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(30, 32);
            this.label3.TabIndex = 48;
            this.label3.Text = "0";
            // 
            // numericUpDown2
            // 
            this.numericUpDown2.Location = new System.Drawing.Point(31, 264);
            this.numericUpDown2.Name = "numericUpDown2";
            this.numericUpDown2.Size = new System.Drawing.Size(128, 20);
            this.numericUpDown2.TabIndex = 49;
            // 
            // numericUpDown3
            // 
            this.numericUpDown3.Location = new System.Drawing.Point(31, 290);
            this.numericUpDown3.Name = "numericUpDown3";
            this.numericUpDown3.Size = new System.Drawing.Size(128, 20);
            this.numericUpDown3.TabIndex = 50;
            // 
            // numericUpDown4
            // 
            this.numericUpDown4.Location = new System.Drawing.Point(31, 316);
            this.numericUpDown4.Name = "numericUpDown4";
            this.numericUpDown4.Size = new System.Drawing.Size(128, 20);
            this.numericUpDown4.TabIndex = 51;
            // 
            // numericUpDown5
            // 
            this.numericUpDown5.Location = new System.Drawing.Point(31, 342);
            this.numericUpDown5.Name = "numericUpDown5";
            this.numericUpDown5.Size = new System.Drawing.Size(128, 20);
            this.numericUpDown5.TabIndex = 52;
            // 
            // numericUpDown6
            // 
            this.numericUpDown6.Location = new System.Drawing.Point(31, 368);
            this.numericUpDown6.Name = "numericUpDown6";
            this.numericUpDown6.Size = new System.Drawing.Size(128, 20);
            this.numericUpDown6.TabIndex = 53;
            // 
            // numericUpDown7
            // 
            this.numericUpDown7.Location = new System.Drawing.Point(31, 394);
            this.numericUpDown7.Name = "numericUpDown7";
            this.numericUpDown7.Size = new System.Drawing.Size(128, 20);
            this.numericUpDown7.TabIndex = 54;
            // 
            // numericUpDown8
            // 
            this.numericUpDown8.Location = new System.Drawing.Point(31, 420);
            this.numericUpDown8.Name = "numericUpDown8";
            this.numericUpDown8.Size = new System.Drawing.Size(128, 20);
            this.numericUpDown8.TabIndex = 55;
            // 
            // numericUpDown9
            // 
            this.numericUpDown9.Location = new System.Drawing.Point(31, 446);
            this.numericUpDown9.Name = "numericUpDown9";
            this.numericUpDown9.Size = new System.Drawing.Size(128, 20);
            this.numericUpDown9.TabIndex = 56;
            // 
            // numericUpDown10
            // 
            this.numericUpDown10.Location = new System.Drawing.Point(31, 472);
            this.numericUpDown10.Name = "numericUpDown10";
            this.numericUpDown10.Size = new System.Drawing.Size(128, 20);
            this.numericUpDown10.TabIndex = 57;
            // 
            // progressBar1
            // 
            this.progressBar1.Location = new System.Drawing.Point(31, 536);
            this.progressBar1.Name = "progressBar1";
            this.progressBar1.Size = new System.Drawing.Size(569, 31);
            this.progressBar1.Step = 1;
            this.progressBar1.Style = System.Windows.Forms.ProgressBarStyle.Continuous;
            this.progressBar1.TabIndex = 58;
            // 
            // trackBar4
            // 
            this.trackBar4.Location = new System.Drawing.Point(187, 461);
            this.trackBar4.Maximum = 100;
            this.trackBar4.Name = "trackBar4";
            this.trackBar4.Size = new System.Drawing.Size(413, 45);
            this.trackBar4.TabIndex = 59;
            this.trackBar4.Scroll += new System.EventHandler(this.trackBar4_Scroll);
            // 
            // label4
            // 
            this.label4.AutoSize = true;
            this.label4.Font = new System.Drawing.Font("Arial", 20F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label4.Location = new System.Drawing.Point(606, 461);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(30, 32);
            this.label4.TabIndex = 60;
            this.label4.Text = "0";
            // 
            // trackBar5
            // 
            this.trackBar5.Location = new System.Drawing.Point(1561, 39);
            this.trackBar5.Maximum = 100;
            this.trackBar5.Name = "trackBar5";
            this.trackBar5.Orientation = System.Windows.Forms.Orientation.Vertical;
            this.trackBar5.Size = new System.Drawing.Size(45, 760);
            this.trackBar5.TabIndex = 62;
            this.trackBar5.Value = 15;
            this.trackBar5.Scroll += new System.EventHandler(this.trackBar5_Scroll);
            // 
            // timer1
            // 
            this.timer1.Interval = 500;
            this.timer1.Tick += new System.EventHandler(this.timer1_Tick);
            // 
            // label9
            // 
            this.label9.AutoSize = true;
            this.label9.Location = new System.Drawing.Point(38, 132);
            this.label9.Name = "label9";
            this.label9.Size = new System.Drawing.Size(28, 14);
            this.label9.TabIndex = 63;
            this.label9.Text = "USB";
            // 
            // grpUsbStatus
            // 
            this.grpUsbStatus.Controls.Add(this.btnMonitor);
            this.grpUsbStatus.Controls.Add(this.lblUsbIndicator);
            this.grpUsbStatus.Controls.Add(this.lblUsbMessage);
            this.grpUsbStatus.Controls.Add(this.label9);
            this.grpUsbStatus.Controls.Add(this.lblUsbStatus);
            this.grpUsbStatus.Location = new System.Drawing.Point(12, 16);
            this.grpUsbStatus.Name = "grpUsbStatus";
            this.grpUsbStatus.Size = new System.Drawing.Size(358, 197);
            this.grpUsbStatus.TabIndex = 65;
            this.grpUsbStatus.TabStop = false;
            this.grpUsbStatus.Text = "USB Status";
            // 
            // lblUsbIndicator
            // 
            this.lblUsbIndicator.BackColor = System.Drawing.Color.DimGray;
            this.lblUsbIndicator.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.lblUsbIndicator.Location = new System.Drawing.Point(35, 157);
            this.lblUsbIndicator.Name = "lblUsbIndicator";
            this.lblUsbIndicator.Size = new System.Drawing.Size(22, 22);
            this.lblUsbIndicator.TabIndex = 2;
            // 
            // lblUsbMessage
            // 
            this.lblUsbMessage.Location = new System.Drawing.Point(38, 92);
            this.lblUsbMessage.Name = "lblUsbMessage";
            this.lblUsbMessage.Size = new System.Drawing.Size(314, 40);
            this.lblUsbMessage.TabIndex = 1;
            this.lblUsbMessage.Text = "Waiting for device...";
            // 
            // lblUsbStatus
            // 
            this.lblUsbStatus.AutoSize = true;
            this.lblUsbStatus.Location = new System.Drawing.Point(35, 45);
            this.lblUsbStatus.Name = "lblUsbStatus";
            this.lblUsbStatus.Size = new System.Drawing.Size(135, 14);
            this.lblUsbStatus.TabIndex = 0;
            this.lblUsbStatus.Text = "USB device not connected";
            // 
            // FrmMain
            // 
            this.ClientSize = new System.Drawing.Size(1633, 811);
            this.Controls.Add(this.grpUsbStatus);
            this.Controls.Add(this.label8);
            this.Controls.Add(this.trackBar5);
            this.Controls.Add(this.label4);
            this.Controls.Add(this.label7);
            this.Controls.Add(this.trackBar4);
            this.Controls.Add(this.label6);
            this.Controls.Add(this.progressBar1);
            this.Controls.Add(this.label5);
            this.Controls.Add(this.numericUpDown10);
            this.Controls.Add(this.numericUpDown9);
            this.Controls.Add(this.numericUpDown8);
            this.Controls.Add(this.numericUpDown7);
            this.Controls.Add(this.numericUpDown6);
            this.Controls.Add(this.numericUpDown5);
            this.Controls.Add(this.numericUpDown4);
            this.Controls.Add(this.numericUpDown3);
            this.Controls.Add(this.numericUpDown2);
            this.Controls.Add(this.label3);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.label1);
            this.Controls.Add(this.groupBox1);
            this.Controls.Add(this.trackBar3);
            this.Controls.Add(this.trackBar2);
            this.Controls.Add(this.trackBar1);
            this.Controls.Add(this.numericUpDown1);
            this.Controls.Add(this.comboBox1);
            this.Font = new System.Drawing.Font("Arial", 8F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedSingle;
            this.Location = new System.Drawing.Point(21, 28);
            this.Name = "FrmMain";
            this.StartPosition = System.Windows.Forms.FormStartPosition.Manual;
            this.Text = "STM32 Bulk / WinUSB Monitor";
            this.Closed += new System.EventHandler(this.frmMain_Closed);
            this.Load += new System.EventHandler(this.frmMain_Load);
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown1)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.trackBar1)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.trackBar2)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.trackBar3)).EndInit();
            this.groupBox1.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.chart1)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown2)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown3)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown4)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown5)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown6)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown7)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown8)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown9)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.numericUpDown10)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.trackBar4)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.trackBar5)).EndInit();
            this.grpUsbStatus.ResumeLayout(false);
            this.grpUsbStatus.PerformLayout();
            this.ResumeLayout(false);
            this.PerformLayout();

		}
		#endregion

		private Boolean _deviceDetected;
		private IntPtr _deviceNotificationHandle;
		private ManagementEventWatcher _deviceArrivedWatcher;
		private Boolean _deviceHandleObtained;
		private ManagementEventWatcher _deviceRemovedWatcher;
		private Int32 _myProductId;
		private Int32 _myVendorId;
		private Boolean _periodicTransfersRequested;
		private Boolean _reconnectInProgress;
		private Boolean _transferInProgress;
		private FrmMonitor _monitorForm;

		private string _usbDeviceName = "unknown";
		private string _usbManufacturer = "unknown";
		private string _usbDeviceId = "unknown";
		private string _usbClassGuid = "unknown";
		private string _usbDriverService = "unknown";
		private string _lastBulkOpenError = "WinUSB interface has not been checked";


		private static System.Timers.Timer _periodicTransfers;

		private readonly DeviceManagement _myDeviceManagement = new DeviceManagement();
		private readonly WinUsbDevice _bulkDevice = new WinUsbDevice();

		private enum WmiDeviceProperties
		{
			Name,
			Caption,
			Description,
			Manufacturer,
			PNPDeviceID,
			DeviceID,
			ClassGUID
		}

		internal FrmMain FrmMy;

		///  <summary>
		///  Add a handler to detect arrival of devices using WMI.
		///  </summary>

		private void AddDeviceArrivedHandler()
		{
			const Int32 pollingIntervalSeconds = 1;
			var scope = new ManagementScope("root\\CIMV2");
			scope.Options.EnablePrivileges = true;

			try
			{
				var q = new WqlEventQuery();
				q.EventClassName = "__InstanceCreationEvent";
				q.WithinInterval = new TimeSpan(0, 0, pollingIntervalSeconds);
				q.Condition = @"TargetInstance ISA 'Win32_USBControllerdevice'";
				_deviceArrivedWatcher = new ManagementEventWatcher(scope, q);
				_deviceArrivedWatcher.EventArrived += DeviceAdded;

				_deviceArrivedWatcher.Start();
			}
			catch (Exception e)
			{
				Debug.WriteLine(e.Message);
				if (_deviceArrivedWatcher != null)
					_deviceArrivedWatcher.Stop();
			}
		}

		///  <summary>
		///  Add a handler to detect removal of devices using WMI.
		///  </summary>

		private void AddDeviceRemovedHandler()
		{
			const Int32 pollingIntervalSeconds = 1;
			var scope = new ManagementScope("root\\CIMV2");
			scope.Options.EnablePrivileges = true;

			try
			{
				var q = new WqlEventQuery();
				q.EventClassName = "__InstanceDeletionEvent";
				q.WithinInterval = new TimeSpan(0, 0, pollingIntervalSeconds);
				q.Condition = @"TargetInstance ISA 'Win32_USBControllerdevice'";
				_deviceRemovedWatcher = new ManagementEventWatcher(scope, q);
				_deviceRemovedWatcher.EventArrived += DeviceRemoved;
				_deviceRemovedWatcher.Start();
			}
			catch (Exception e)
			{
				Debug.WriteLine(e.Message);
				if (_deviceRemovedWatcher != null)
					_deviceRemovedWatcher.Stop();
			}
		}

		private void CloseCommunications()
		{
			try
			{
				_bulkDevice.Dispose();
			}
			catch (Exception ex)
			{
				Debug.WriteLine("CloseCommunications: " + ex.Message);
			}
			finally
			{
				_deviceHandleObtained = false;
				_transferInProgress = false;
			}
		}




		private void DeviceAdded(object sender, EventArrivedEventArgs e)
		{
			try
			{
				Debug.WriteLine("A USB device has been inserted");

				BeginInvoke((MethodInvoker)delegate
				{
					if (_deviceHandleObtained)
					{
						return;
					}

					ReconnectBulkDevice();
				});
			}
			catch (Exception ex)
			{
				Debug.WriteLine("DeviceAdded error: " + ex.Message);
			}
		}

		private async void ReconnectBulkDevice()
		{
			if (_reconnectInProgress)
			{
				return;
			}

			_reconnectInProgress = true;
			try
			{
				/* Let Windows finish removal/re-enumeration before opening the new
				   interface path. A WMI arrival notification can precede WinUSB. */
				await Task.Delay(400);
				for (Int32 retry = 0; retry < 20; retry++)
				{
					if (IsDisposed || Disposing)
					{
						return;
					}

					_deviceDetected = FindDeviceUsingWmi();
					_deviceHandleObtained = _deviceDetected && FindTheBulkDevice();
					if (_deviceHandleObtained)
					{
						/* Configuration is complete, but give the firmware Bulk thread one
						   scheduler slice before sending the first 64-byte request. */
						await Task.Delay(100);
						StartUsbPollingAutomatically();
						AddUsbEvent("USB Bulk device reconnected");
						return;
					}

					await Task.Delay(250);
				}

				AddUsbEvent("WinUSB connection failed: " + _lastBulkOpenError);
			}
			finally
			{
				_reconnectInProgress = false;
			}
		}

		/////////////////////////////////////////////////////////////////////////////////////////////////////////
		///  <summary>
		///  Called if the user changes the Vendor ID or Product ID in the text box.
		///  </summary>

		private void DeviceHasChanged()
		{
			try
			{
				//  If a device was previously detected, stop receiving notifications about it.

				if (_deviceHandleObtained)
				{
					DeviceNotificationsStop();

					CloseCommunications();
				}
				// Look for a device that matches the Vendor ID and Product ID in the text boxes.

				FindTheBulkDevice();
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  Add handlers to detect device arrival and removal.
        ///  </summary>

        private void DeviceNotificationsStart()
		{
			AddDeviceArrivedHandler();
			AddDeviceRemovedHandler();
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  Stop receiving notifications about device arrival and removal
        ///  </summary>

        private void DeviceNotificationsStop()
		{
			try
			{
				if (_deviceArrivedWatcher != null)
					_deviceArrivedWatcher.Stop();
				if (_deviceRemovedWatcher != null)
					_deviceRemovedWatcher.Stop();
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  Called on removal of any device.
        ///  Calls a routine that searches to see if the desired device is still present.
        ///  </summary>
        /// 
        private void DeviceRemoved(object sender, EventArgs e)
		{
			try
			{
				Debug.WriteLine("A USB device has been removed");
				BeginInvoke((MethodInvoker)delegate
				{
					_deviceDetected = FindDeviceUsingWmi();
					if (!_deviceDetected)
					{
						HandleUsbDisconnect("device removed");
					}
				});

			}
			catch (Exception ex)
			{
				Debug.WriteLine("DeviceRemoved error: " + ex.Message);
			}
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  Do periodic transfers.
        ///  </summary>
        /// <param name="source"></param>
        /// <param name="e"></param>
        ///  <remarks>
        ///  The timer is enabled only if continuous (periodic) transfers have been requested.
        ///  </remarks>		  

        private void DoPeriodicTransfers(object source, ElapsedEventArgs e)
		{
			try
			{
				PeriodicTransfers();
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        /// <summary>
        /// Enable the command buttons on the form.
        /// Needed after attempting a transfer and device not found.
        /// </summary>
        /// 
        private void EnableFormControls()
		{
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  Use the System.Management class to find a device by Vendor ID and Product ID using WMI. If found, display device properties.
        ///  </summary>
        /// <remarks> 
        /// During debugging, if you stop the firmware but leave the device attached, the device may still be detected as present
        /// but will be unable to communicate. The device will show up in Windows Device Manager as well. 
        /// This situation is unlikely to occur with a final product.
        /// </remarks>

        private Boolean FindDeviceUsingWmi()
		{
			try
			{
				// Prepend "@" to string below to treat backslash as a normal character (not escape character):

				String deviceIdString = @"USB\VID_" + _myVendorId.ToString("X4") + "&PID_" + _myProductId.ToString("X4");

				_deviceDetected = false;
				var searcher = new ManagementObjectSearcher("root\\CIMV2", "SELECT * FROM Win32_PnPEntity");

				foreach (ManagementObject queryObj in searcher.Get())
				{
					String pnpDeviceId = Convert.ToString(queryObj["PNPDeviceID"]);
					if (pnpDeviceId.IndexOf(
						deviceIdString,
						StringComparison.OrdinalIgnoreCase) >= 0)
					{
						_deviceDetected = true;
						_usbDeviceName = Convert.ToString(queryObj["Name"]);

						_usbManufacturer = Convert.ToString(queryObj["Manufacturer"]);

						_usbDeviceId = Convert.ToString(queryObj["DeviceID"]);

						_usbClassGuid = Convert.ToString(queryObj["ClassGUID"]);
						_usbDriverService = Convert.ToString(queryObj["Service"]);

						AddUsbEvent("USB device found by WMI");

						if (_monitorForm != null && !_monitorForm.IsDisposed)
						{
							_monitorForm.SetDeviceInfo(
								_usbDeviceName,
								_usbManufacturer,
								_myVendorId,
								_myProductId,
								_usbDeviceId,
								_usbClassGuid);
						}

						Debug.WriteLine("USB device found by WMI");

						foreach (WmiDeviceProperties property in Enum.GetValues(typeof(WmiDeviceProperties)))
						{
							Debug.WriteLine(
								property + ": " +
								queryObj[property.ToString()]);
						}
					}
				}
				if (!_deviceDetected)
				{
					Debug.WriteLine("My device not found (WMI)");
				}
				return _deviceDetected;
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
		}
		/// <summary>
		/// Finds the STM32 WinUSB interface and opens its Bulk IN/OUT pipes.
		/// </summary>
		private Boolean FindTheBulkDevice()
		{
			String vidText = "vid_" + _myVendorId.ToString("x4");
			String pidText = "pid_" + _myProductId.ToString("x4");
			Boolean interfaceFound = false;
			Boolean matchingPathFound = false;
			String openError = String.Empty;

			CloseCommunications();

			foreach (Guid interfaceGuid in UsbInterfaceDiscovery.FindInterfaceGuids(
				_myVendorId, _myProductId))
			{
				var devicePathNames = new String[128];
				Boolean interfacesAvailable;
				try
				{
					interfacesAvailable = _myDeviceManagement.FindDeviceFromGuid(
						interfaceGuid,
						ref devicePathNames);
				}
				catch (Exception ex)
				{
					openError = "SetupAPI " + interfaceGuid.ToString("B") +
						": " + ex.Message;
					continue;
				}

				if (!interfacesAvailable)
				{
					continue;
				}

				interfaceFound = true;
				foreach (String devicePath in devicePathNames)
				{
					if (String.IsNullOrEmpty(devicePath))
					{
						continue;
					}

					String lowerPath = devicePath.ToLowerInvariant();
					if (!lowerPath.Contains(vidText) || !lowerPath.Contains(pidText))
					{
						continue;
					}
					matchingPathFound = true;

					if (_bulkDevice.Open(devicePath))
					{
						_deviceDetected = true;
						_deviceHandleObtained = true;
						_lastBulkOpenError = String.Empty;
						AddUsbEvent("WinUSB Bulk opened, VID=" +
							_myVendorId.ToString("X4") + " PID=" +
							_myProductId.ToString("X4") + " GUID=" +
							interfaceGuid.ToString("B"));
						SetUsbStatus(
							"USB Bulk device connected",
							"WinUSB, OUT 0x01 / IN 0x81",
							Color.LimeGreen);
						return true;
					}

					openError = _bulkDevice.LastError;
				}
			}

			_deviceHandleObtained = false;
			if (!interfaceFound || !matchingPathFound)
			{
				_lastBulkOpenError = _deviceDetected
					? "USB device is present (driver: " + _usbDriverService +
						"), but its WinUSB data interface is not registered"
					: "VID/PID not found in registered USB interfaces";
			}
			else if (!String.IsNullOrEmpty(openError))
			{
				_lastBulkOpenError = openError;
			}
			else
			{
				_lastBulkOpenError = "WinUSB interface found but could not be opened";
			}
			SetUsbStatus(
				"USB Bulk device not connected",
				_lastBulkOpenError,
				Color.DimGray);
			return false;
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  Perform shutdown operations.
        ///  </summary>

        private void frmMain_Closed(Object eventSender, EventArgs eventArgs)
		{
			try
			{
				Shutdown();
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  Perform startup operations.
        ///  </summary>

        private void frmMain_Load(Object eventSender, EventArgs eventArgs)
		{
            trackBar1.Value = 35;
            label1.Text = trackBar1.Value.ToString();
            trackBar2.Value = 50;
            label2.Text = trackBar2.Value.ToString();
            trackBar3.Value = 25;
            label3.Text = trackBar3.Value.ToString();

            try
			{
				FrmMy = this;
				Startup();
				StartUsbPollingAutomatically();
				if (_deviceHandleObtained)
				{
					AddUsbEvent("USB device connected");
				}
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
            timer1.Start();
        }

        /////////////////////////////////////////////////////////////////////////////////////////////////////////
		/// <summary>
		/// Alternat sending and getting a report.
		/// </summary>

		private void PeriodicTransfers()
		{
			try
			{
				if (!_transferInProgress)
				{
					RequestBulkExchange();
				}
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        /// <summary>
        /// Start doing periodic transfers.
        /// </summary>

        private void PeriodicTransfersStart()
		{
			// Don't allow changing the transfer type while transfers are in progress.

			//  Change the command button's text.


			//  Enable the timer event to trigger a set of transfers.

			_periodicTransfers.Start();

			_periodicTransfersRequested = true;
			PeriodicTransfers();
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        /// <summary>
        /// Stop doing periodic transfers.
        /// </summary>

        private void PeriodicTransfersStop()
		{
			//  Stop doing continuous transfers.

			_periodicTransfersRequested = false;

			// Disable the timer that triggers the transfers.	

			_periodicTransfers.Stop();

			// Re-allow changing the transfer type.

		}

		private void radInputOutputControl_CheckedChanged(object sender, EventArgs e)
		{
		}

		private void radInputOutputInterrupt_CheckedChanged(object sender, EventArgs e)
		{
		}

		private void radFeature_CheckedChanged(object sender, EventArgs e)
		{
		}


        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
		/// Sends one 64-byte Bulk command and reads the matching 64-byte response.
		/// Unlike HID, Bulk packets do not contain a report-ID byte.
		/// </summary>

		private async void RequestBulkExchange()
		{
			const Int32 packetSize = 64;
			const Int32 sensorDataOffset = 5;

			if (_transferInProgress)
			{
				return;
			}

			try
			{
				if (!_deviceHandleObtained || !_bulkDevice.IsOpen)
				{
					_deviceHandleObtained = FindTheBulkDevice();
				}

				if (!_deviceHandleObtained || !_bulkDevice.IsOpen)
				{
					return;
				}

				var txBuffer = new Byte[packetSize];
				txBuffer[0] = Convert.ToByte(numericUpDown1.Value);
				txBuffer[1] = Convert.ToByte(trackBar1.Value);
				txBuffer[2] = Convert.ToByte(trackBar2.Value);
				txBuffer[3] = Convert.ToByte(trackBar3.Value);
				txBuffer[4] = 0;

				_transferInProgress = true;

				Int32 bytesWritten = await _bulkDevice.WriteAsync(txBuffer);
				if (bytesWritten != packetSize)
				{
					throw new IOException("Incomplete Bulk OUT packet: " + bytesWritten);
				}

				if (_monitorForm != null && !_monitorForm.IsDisposed)
				{
					_monitorForm.ShowTx(txBuffer);
				}

				var rxBuffer = new Byte[packetSize];
				Int32 bytesRead = await _bulkDevice.ReadAsync(rxBuffer);
				if (bytesRead < sensorDataOffset + 12)
				{
					throw new IOException("Incomplete Bulk IN packet: " + bytesRead);
				}

				if (_monitorForm != null && !_monitorForm.IsDisposed)
				{
					_monitorForm.ShowRx(rxBuffer);
				}

				Int32 temperature = BitConverter.ToInt32(rxBuffer, sensorDataOffset);
				UInt32 pressure = BitConverter.ToUInt32(rxBuffer, sensorDataOffset + 4);
				UInt32 humidity = BitConverter.ToUInt32(rxBuffer, sensorDataOffset + 8);

				label5.Text = temperature.ToString();
				label6.Text = pressure.ToString();
				label7.Text = humidity.ToString();
			}
			catch (Exception ex)
			{
				Debug.WriteLine("USB Bulk exchange error: " + ex);
				HandleUsbDisconnect(ex.Message);
			}
			finally
			{
				_transferInProgress = false;
			}
		}

        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  Perform actions that must execute when the program ends.
        ///  </summary>

        private void Shutdown()
		{
			try
			{
				CloseCommunications();
				DeviceNotificationsStop();
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  Perform actions that must execute when the program starts.
        ///  </summary>

        private void Startup()
		{
			//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			const Int32 periodicTransferInterval = 20;	// one Bulk request/response every 20 ms
			try
			{
			//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				_periodicTransfers = new System.Timers.Timer(periodicTransferInterval);
				_periodicTransfers.Elapsed += DoPeriodicTransfers;
				_periodicTransfers.Stop();
				_periodicTransfers.SynchronizingObject = this;

				//  Default USB Vendor ID and Product ID:

				_myVendorId  = 0x0483;
				_myProductId = 0x5752;

				DeviceNotificationsStart();
				FindDeviceUsingWmi();
				FindTheBulkDevice();
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  The Product ID has changed in the text box. Call a routine to handle it.
        ///  </summary>

        private void txtProductID_TextChanged(Object sender, EventArgs e)
		{
			try
			{
				DeviceHasChanged();
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  The Vendor ID has changed in the text box. Call a routine to handle it.
        ///  </summary>

        private void txtVendorID_TextChanged(Object sender, EventArgs e)
		{
			try
			{
				DeviceHasChanged();
			}
			catch (Exception ex)
			{
				DisplayException(Name, ex);
				throw;
			}
		}
        /////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///  <summary>
        ///  Provides a central mechanism for exception handling.
        ///  Displays a message box that describes the exception.
        ///  </summary>
        ///  
        ///  <param name="moduleName"> the module where the exception occurred. </param>
        ///  <param name="e"> the exception </param>

        internal static void DisplayException(String moduleName, Exception e)
		{
			//  Create an error message.

			String message = "Exception: " + e.Message + Environment.NewLine + "Module: " + moduleName + Environment.NewLine + "Method: " + e.TargetSite.Name;

			const String caption = "Unexpected Exception";

			MessageBox.Show(message, caption, MessageBoxButtons.OK);
			Debug.Write(message);

			// Get the last error and display it. 

			Int32 error = Marshal.GetLastWin32Error();

			Debug.WriteLine("The last Win32 Error was: " + error);
		}

        private void trackBar1_Scroll(object sender, EventArgs e)
        {
            label1.Text = trackBar1.Value.ToString();
		}

        private void trackBar2_Scroll(object sender, EventArgs e)
        {
            label2.Text = trackBar2.Value.ToString();
		}

        private void trackBar3_Scroll(object sender, EventArgs e)
        {
            label3.Text = trackBar3.Value.ToString();
		}

        private void trackBar4_Scroll(object sender, EventArgs e)
        {
            progressBar1.Value = trackBar4.Value;
        }

        private void trackBar5_Scroll(object sender, EventArgs e)
        {
        }

        private double[] logarray = new double[60];

        private void Updatechart1()
        {
            logarray[logarray.Length - 1] = trackBar5.Value;
            Array.Copy(logarray, 1, logarray, 0, logarray.Length - 1);
            chart1.Series["Series1"].Points.Clear();
            for (int i = 0; i < logarray.Length - 1; ++i)
            {
                chart1.Series["Series1"].Points.AddY(logarray[i]);
            }

            if ((_deviceHandleObtained && _bulkDevice.IsOpen) || _deviceDetected)
            {
                label9.Text = ("USB Device in system");
            }

            if (!_deviceDetected)
			{
                label9.Text = ("USB Device not connected");
            }
        }

        private void timer1_Tick(object sender, EventArgs e)
        {
            Updatechart1();
        }
        private void button1_Click(object sender, EventArgs e)
        {
            chart1.Series["Series1"].Points.Clear();
        }

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		/*
        to save settings from some certain object's settings in form 
        _close
        My.Settings.volume - form3.trackBar1.Value
        */
		/// <summary>
		/// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		/// </summary>
		[STAThread]
		internal static void Main() { Application.Run(new FrmMain()); }
		private static FrmMain _transDefaultFormFrmMain;
		internal static FrmMain TransDefaultFormFrmMain
		{
			get
			{
				if (_transDefaultFormFrmMain == null)
				{
					_transDefaultFormFrmMain = new FrmMain();
				}
				return _transDefaultFormFrmMain;
			}
		}


		private void TrackBar_Send_MouseUp(object sender, MouseEventArgs e)
		{
			if (_transferInProgress)
			{
				AddUsbEvent("MouseUp blocked: transfer already in progress");
				return;
			}

			if (!_deviceHandleObtained)
			{
				AddUsbEvent("MouseUp blocked: device handle not obtained");
				return;
			}

			if (!_bulkDevice.IsOpen)
			{
				AddUsbEvent("MouseUp blocked: WinUSB Bulk handle is closed");
				return;
			}

			RequestBulkExchange();
		}


		private void HandleUsbDisconnect(string reason)
		{
			try
			{
				bool wasConnected = _deviceDetected || _deviceHandleObtained || _bulkDevice.IsOpen;

				if (!wasConnected)
				{
					Debug.WriteLine("Duplicate USB disconnect ignored: " + reason);

					return;
				}
				_deviceDetected = false;
				_deviceHandleObtained = false;
				_transferInProgress = false;

				if (_periodicTransfersRequested)
				{
					PeriodicTransfersStop();
				}

				CloseCommunications();

				SetUsbStatus("USB device not connected", "USB disconnected: " + reason, Color.DimGray);
				AddUsbEvent("USB device disconnected (" + reason + ")");

				EnableFormControls();

				/* A transfer can fail before WMI reports removal/arrival. Always
				   start a bounded re-open sequence so a cable reconnect does not
				   require pressing Find Device. */
				if (!IsDisposed && !Disposing && IsHandleCreated)
				{
					BeginInvoke((MethodInvoker)delegate
					{
						ReconnectBulkDevice();
					});
				}
			}
			catch (Exception ex)
			{
				Debug.WriteLine("HandleUsbDisconnect: " + ex.Message);
			}
		}


		private void StartUsbPollingAutomatically()
		{
			if (!_deviceHandleObtained)
			{
				return;
			}

			_transferInProgress = false;
			_periodicTransfersRequested = false;

			if (_periodicTransfers != null)
			{
				_periodicTransfers.Stop();
			}

			if (_periodicTransfersRequested)
			{
				return;
			}

			PeriodicTransfersStart();
		}

		private void btnMonitor_Click(object sender, EventArgs e)
		{
			if (_monitorForm == null || _monitorForm.IsDisposed)
			{
				_monitorForm = new FrmMonitor();
				_monitorForm.FindDeviceRequested += Monitor_FindDeviceRequested;
				_monitorForm.TransferToggleRequested += Monitor_TransferToggleRequested;
				_monitorForm.Show(this);
				_monitorForm.SetDeviceInfo(
				_usbDeviceName,
				_usbManufacturer,
				_myVendorId,
				_myProductId,
				_usbDeviceId,
				_usbClassGuid);
			}
			else
			{
				_monitorForm.BringToFront();
				_monitorForm.Activate();
			}
		}

		private void Monitor_FindDeviceRequested(string vendorIdText, string productIdText)
		{
			try
			{
				int vendorId = int.Parse(vendorIdText, NumberStyles.AllowHexSpecifier);

				int productId = int.Parse(productIdText, NumberStyles.AllowHexSpecifier);

				_myVendorId = vendorId;
				_myProductId = productId;

				if (_periodicTransfersRequested)
				{
					PeriodicTransfersStop();
				}

				CloseCommunications();

				_deviceDetected = FindDeviceUsingWmi();
				_deviceHandleObtained = FindTheBulkDevice();

				if (_deviceHandleObtained)
				{
					StartUsbPollingAutomatically();
					AddUsbEvent("Connected to selected VID/PID");
				}
				else
				{
					AddUsbEvent("Connection failed: " + _lastBulkOpenError);
				}
			}
			catch (FormatException)
			{
				AddUsbEvent("Invalid VID or PID format");
			}
			catch (Exception ex)
			{
				Debug.WriteLine("Monitor_FindDeviceRequested: " + ex);

				AddUsbEvent("Failed to connect: " + ex.Message);
			}
		}

		private void Monitor_TransferToggleRequested()
		{
			if (_periodicTransfersRequested)
			{
				PeriodicTransfersStop();

				if (_monitorForm != null &&
					!_monitorForm.IsDisposed)
				{
					_monitorForm.SetTransfersRunning(false);
				}

				AddUsbEvent("Transfers stopped");
			}
			else
			{
				if (!_deviceHandleObtained)
				{
					AddUsbEvent("Cannot start transfers: device not connected");

					return;
				}

				PeriodicTransfersStart();

				if (_monitorForm != null && !_monitorForm.IsDisposed)
				{
					_monitorForm.SetTransfersRunning(true);
				}

				AddUsbEvent("Transfers started");
			}
		}

		private void SetUsbStatus(string status, string message, Color indicatorColor)
		{
			if (InvokeRequired)
			{
				BeginInvoke(new Action<string, string, Color>(
					SetUsbStatus),
					status,
					message,
					indicatorColor);

				return;
			}

			lblUsbStatus.Text = status;
			lblUsbMessage.Text = message;
			lblUsbIndicator.BackColor = indicatorColor;
        }

		private void AddUsbEvent(string message)
		{
			Debug.WriteLine(message);

			if (_monitorForm != null && !_monitorForm.IsDisposed)
			{
				_monitorForm.AddEvent(message);
			}
		}
	}
}
