using System;
using System.Drawing;
using System.Windows.Forms;

namespace GenericHid
{
    public class FrmMonitor : Form
    {
        private readonly Label _txStatus;
        private readonly Label _txData;
        private readonly Label _txIndicator;

        private readonly Label _rxStatus;
        private readonly Label _rxData;
        private readonly Label _rxIndicator;

        private readonly Timer _txBlinkTimer;
        private readonly Timer _rxBlinkTimer;

        private readonly ListBox _eventLog;
        private readonly Label _deviceInfo;
        
        private string _usbDeviceName = "unknown";
        private string _usbManufacturer = "unknown";
        private string _usbDeviceId = "unknown";
        private string _usbClassGuid = "unknown";

        private readonly TextBox _txtVendorId;
        private readonly TextBox _txtProductId;
        private readonly Button _btnFindDevice;
        private readonly Button _btnTransferToggle;

        public event Action<string, string> FindDeviceRequested;
        public event Action TransferToggleRequested;

        public FrmMonitor()
        {
            Text = "USB Monitor";
            StartPosition = FormStartPosition.CenterParent;
            ClientSize = new Size(800, 700);

            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            MinimizeBox = true;

            BackColor = Color.FromArgb(230, 230, 230);

            // TX block
            var txPanel = new Panel
            {
                Location = new Point(20, 20),
                Size = new Size(760, 120),
                BackColor = Color.FromArgb(200, 200, 200),
                BorderStyle = BorderStyle.FixedSingle
            };

            _txStatus = new Label
            {
                Location = new Point(15, 12),
                Size = new Size(620, 24),
                Text = "TX: waiting",
                Font = new Font("Consolas", 10, FontStyle.Bold)
            };

            _txData = new Label
            {
                Location = new Point(15, 45),
                Size = new Size(700, 50),
                Text = "00 00 00 00 00",
                Font = new Font("Consolas", 11),
                AutoEllipsis = true
            };

            _txIndicator = new Label
            {
                Location = new Point(710, 12),
                Size = new Size(24, 24),
                BackColor = Color.DarkGreen,
                BorderStyle = BorderStyle.FixedSingle
            };

            txPanel.Controls.Add(_txStatus);
            txPanel.Controls.Add(_txData);
            txPanel.Controls.Add(_txIndicator);

            _txIndicator.BackColor = Color.DimGray;

            _txBlinkTimer = new Timer();
            _txBlinkTimer.Interval = 120;
            _txBlinkTimer.Tick += TxBlinkTimer_Tick;

            // RX block
            var rxPanel = new Panel
            {
                Location = new Point(20, 160),
                Size = new Size(760, 120),
                BackColor = Color.FromArgb(200, 200, 200),
                BorderStyle = BorderStyle.FixedSingle
            };

            _rxStatus = new Label
            {
                Location = new Point(15, 12),
                Size = new Size(620, 24),
                Text = "RX: waiting",
                Font = new Font("Consolas", 10, FontStyle.Bold)
            };

            _rxData = new Label
            {
                Location = new Point(15, 45),
                Size = new Size(700, 50),
                Text = "00 00 00 00 00",
                Font = new Font("Consolas", 11),
                AutoEllipsis = true
            };

            _rxIndicator = new Label
            {
                Location = new Point(710, 12),
                Size = new Size(24, 24),
                BackColor = Color.DarkGreen,
                BorderStyle = BorderStyle.FixedSingle
            };

            _rxIndicator.BackColor = Color.DimGray;

            _rxBlinkTimer = new Timer();
            _rxBlinkTimer.Interval = 120;
            _rxBlinkTimer.Tick += RxBlinkTimer_Tick;

            rxPanel.Controls.Add(_rxStatus);
            rxPanel.Controls.Add(_rxData);
            rxPanel.Controls.Add(_rxIndicator);

            Controls.Add(txPanel);
            Controls.Add(rxPanel);

            // Device Info block
            var deviceInfoPanel = new Panel
            {
                Location = new Point(20, 300),
                Size = new Size(760, 150),
                BackColor = Color.FromArgb(200, 200, 200),
                BorderStyle = BorderStyle.FixedSingle
            };

            var deviceInfoTitle = new Label
            {
                Location = new Point(15, 10),
                Size = new Size(300, 24),
                Text = "Device Info",
                Font = new Font("Consolas", 10, FontStyle.Bold)
            };

            _deviceInfo = new Label
            {
                Location = new Point(15, 38),
                Size = new Size(335, 95),
                Text =
                    "Name: unknown\r\n" +
                    "Manufacturer: unknown\r\n" +
                    "VID: ----  PID: ----\r\n" +
                    "Device ID: unknown\r\n" +
                    "Class GUID: unknown",
                Font = new Font("Consolas", 9),
                AutoEllipsis = true
            };

            deviceInfoPanel.Controls.Add(deviceInfoTitle);
            deviceInfoPanel.Controls.Add(_deviceInfo);

            Controls.Add(deviceInfoPanel);


            var logPanel = new Panel
            {
                Location = new Point(20, 470),
                Size = new Size(760, 210),
                BackColor = Color.FromArgb(200, 200, 200),
                BorderStyle = BorderStyle.FixedSingle
            };

            var logTitle = new Label
            {
                Location = new Point(15, 10),
                Size = new Size(300, 24),
                Text = "USB Event Log",
                Font = new Font("Consolas", 10, FontStyle.Bold)
            };

            _eventLog = new ListBox
            {
                Location = new Point(15, 40),
                Size = new Size(728, 150),
                Font = new Font("Consolas", 9),
                HorizontalScrollbar = true
            };

            logPanel.Controls.Add(logTitle);
            logPanel.Controls.Add(_eventLog);

            Controls.Add(logPanel);


            var connectionPanel = new GroupBox
            {
                Location = new Point(395, 10),
                Size = new Size(345, 125),
                Text = "Connection / Debug",
                Font = new Font("Consolas", 9, FontStyle.Bold)
            };

            var vendorLabel = new Label
            {
                Location = new Point(15, 28),
                Size = new Size(90, 22),
                Text = "VID (hex):",
                Font = new Font("Consolas", 9)
            };

            _txtVendorId = new TextBox
            {
                Location = new Point(105, 25),
                Size = new Size(75, 22),
                Text = "0483",
                CharacterCasing = CharacterCasing.Upper
            };

            var productLabel = new Label
            {
                Location = new Point(15, 58),
                Size = new Size(90, 22),
                Text = "PID (hex):",
                Font = new Font("Consolas", 9)
            };

            _txtProductId = new TextBox
            {
                Location = new Point(105, 55),
                Size = new Size(75, 22),
                Text = "5752",
                CharacterCasing = CharacterCasing.Upper
            };

            _btnFindDevice = new Button
            {
                Location = new Point(195, 24),
                Size = new Size(130, 54),
                Text = "Find Device"
            };

            _btnTransferToggle = new Button
            {
                Location = new Point(15, 88),
                Size = new Size(310, 27),
                Text = "Stop transfers"
            };

            connectionPanel.Controls.Add(vendorLabel);
            connectionPanel.Controls.Add(_txtVendorId);
            connectionPanel.Controls.Add(productLabel);
            connectionPanel.Controls.Add(_txtProductId);
            connectionPanel.Controls.Add(_btnFindDevice);
            connectionPanel.Controls.Add(_btnTransferToggle);



            _btnFindDevice.Click += (sender, e) =>
            {
                if (FindDeviceRequested != null)
                {
                    FindDeviceRequested(
                        _txtVendorId.Text.Trim(),
                        _txtProductId.Text.Trim());
                }
            };

            _btnTransferToggle.Click += (sender, e) =>
            {
                if (TransferToggleRequested != null)
                {
                    TransferToggleRequested();
                }
            };

            deviceInfoPanel.Controls.Add(connectionPanel);

        }

        public void ShowTx(byte[] data)
        {
            if (InvokeRequired)
            {
                BeginInvoke(new System.Action<byte[]>(ShowTx), data);
                return;
            }

            _txStatus.Text = "TX: transmitted";
            _txData.Text = System.BitConverter.ToString(data).Replace("-", " ");

            _txIndicator.BackColor = Color.LimeGreen;

            _txBlinkTimer.Stop();
            _txBlinkTimer.Start();
        }

        public void ShowRx(byte[] data)
        {
            if (InvokeRequired)
            {
                BeginInvoke(new System.Action<byte[]>(ShowRx), data);
                return;
            }

            _rxStatus.Text = "RX: received";
            _rxData.Text = System.BitConverter.ToString(data).Replace("-", " ");

            _rxIndicator.BackColor = Color.LimeGreen;

            _rxBlinkTimer.Stop();
            _rxBlinkTimer.Start();
        }

        private void TxBlinkTimer_Tick(object sender, System.EventArgs e)
        {
            _txBlinkTimer.Stop();
            _txIndicator.BackColor = Color.DimGray;
        }

        private void RxBlinkTimer_Tick(object sender, System.EventArgs e)
        {
            _rxBlinkTimer.Stop();
            _rxIndicator.BackColor = Color.DimGray;
        }

        private void InitializeComponent()
        {
            this.SuspendLayout();
            // 
            // FrmMonitor
            // 
            this.ClientSize = new System.Drawing.Size(924, 597);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedDialog;
            this.Name = "FrmMonitor";
            this.ResumeLayout(false);

        }
        ////////////////////////////////////


        public void AddEvent(string message)
        {
            if (InvokeRequired)
            {
                BeginInvoke(new System.Action<string>(AddEvent), message);
                return;
            }

            string line =
                System.DateTime.Now.ToString("HH:mm:ss") +
                "  " +
                message;

            _eventLog.Items.Add(line);

            if (_eventLog.Items.Count > 300)
            {
                _eventLog.Items.RemoveAt(0);
            }

            _eventLog.SelectedIndex = _eventLog.Items.Count - 1;
        }


        public void SetDeviceInfo(
        string name,
        string manufacturer,
        int vendorId,
        int productId,
        string deviceId,
        string classGuid)
        {
            if (InvokeRequired)
            {
                BeginInvoke(
                    new System.Action<string, string, int, int, string, string>(SetDeviceInfo),
                    name,
                    manufacturer,
                    vendorId,
                    productId,
                    deviceId,
                    classGuid);
                return;
            }

            _deviceInfo.Text =
            "Name: " + name + "\r\n" +
            "Manufacturer: " + manufacturer + "\r\n" +
            "VID: " + vendorId.ToString("X4") +
            "  PID: " + productId.ToString("X4") + "\r\n" +
            "Device ID: " + deviceId + "\r\n" +
            "Class GUID: " + classGuid;
        }

        public void SetConnectionIds(int vendorId, int productId)
        {
            if (InvokeRequired)
            {
                BeginInvoke(
                    new Action<int, int>(SetConnectionIds),
                    vendorId,
                    productId);

                return;
            }

            _txtVendorId.Text = vendorId.ToString("X4");
            _txtProductId.Text = productId.ToString("X4");
        }

        public void SetTransfersRunning(bool running)
        {
            if (InvokeRequired)
            {
                BeginInvoke(
                    new Action<bool>(SetTransfersRunning),
                    running);

                return;
            }

            _btnTransferToggle.Text =
                running ? "Stop transfers" : "Start transfers";
        }

    }
}
