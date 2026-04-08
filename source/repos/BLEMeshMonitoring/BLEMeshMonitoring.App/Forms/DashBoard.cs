using BLEMeshMonitoring.Logic.Services;
using BLEMeshMonitoring.DataAccess.Models;
using System.Data;
using MQTTnet;
using MQTTnet.Client;
using System.Text.Json;

namespace BLEMeshMonitoring.App.Forms
{
    public partial class DashBoard : Form
    {
        private readonly ESP32Service _espService = new();
        private readonly SensorDataService _sensorService = new();
        private IMqttClient _mqttClient;

        private readonly string[][] deviceData = new string[][]
        {
            new[] { "ESP32_Node_1", "A4:CF:12:8B:01", "Corner 1 — Top left", "42.30", "3.28", "125", "12d 4h 32m", "-65 dBm", "Node", "Just now", "Online" },
            new[] { "ESP32_Node_2", "A4:CF:12:8B:02", "Corner 2 — Top right", "44.10", "3.31", "130", "12d 4h 30m", "-58 dBm", "Node", "2 min ago", "Online" },
            new[] { "ESP32_Node_3", "A4:CF:12:8B:03", "Corner 3 — Bottom left", "41.70", "3.25", "118", "8d 11h 15m", "-82 dBm", "Node", "5 min ago", "Weak" },
            new[] { "ESP32_Node_4", "A4:CF:12:8B:04", "Corner 4 — Bottom right", "43.50", "3.29", "127", "12d 4h 28m", "-61 dBm", "Node", "Just now", "Online" }
        };

        // [temp, hum, voc, aqi, mcu_temp, volt]
        private readonly float[][] sensorData = new float[][]
        {
            new[] { 27.8f, 63.5f, 135f, 42f, 42.3f, 3.28f },
            new[] { 29.1f, 66.8f, 148f, 47f, 44.1f, 3.31f },
            new[] { 28.3f, 64.1f, 139f, 44f, 41.7f, 3.25f },
            new[] { 28.9f, 66.3f, 145f, 48f, 43.5f, 3.29f }
        };

        // Lịch sử realtime (50 điểm mỗi node)
        private readonly List<float>[] tempHistory = new List<float>[4];
        private readonly List<float>[] humHistory = new List<float>[4];
        private readonly List<float>[] mcuTempHistory = new List<float>[4];
        private readonly List<float>[] voltHistory = new List<float>[4];
        private const int MAX_HISTORY = 50;

        public DashBoard()
        {
            InitializeComponent();
            for (int i = 0; i < 4; i++)
            {
                tempHistory[i] = new List<float> { sensorData[i][0] };
                humHistory[i] = new List<float> { sensorData[i][1] };
                mcuTempHistory[i] = new List<float> { sensorData[i][4] };
                voltHistory[i] = new List<float> { sensorData[i][5] };
            }
            SetupSidebar();
            LoadComboBox();
            LoadOverviewData();
            ShowTab("overview");
            ConnectMQTT();
        }

        // ==================== MQTT ====================
        private async void ConnectMQTT()
        {
            var factory = new MqttFactory();
            _mqttClient = factory.CreateMqttClient();

            var options = new MqttClientOptionsBuilder()
                .WithTcpServer("127.0.0.1", 1883)
                .WithClientId("BLEMeshApp")
                .Build();

            _mqttClient.ApplicationMessageReceivedAsync += e =>
            {
                var topic = e.ApplicationMessage.Topic;
                var payload = System.Text.Encoding.UTF8.GetString(e.ApplicationMessage.PayloadSegment);
                this.Invoke(() => { ProcessMQTTData(topic, payload); });
                return Task.CompletedTask;
            };

            _mqttClient.ConnectedAsync += async e =>
            {
                await _mqttClient.SubscribeAsync("ble/node/+/sensor");
                this.Invoke(() => { label3.Text = "MQTT Connected"; label3.ForeColor = Color.Green; });
            };

            _mqttClient.DisconnectedAsync += e =>
            {
                this.Invoke(() => { label3.Text = "MQTT Disconnected"; label3.ForeColor = Color.Red; });
                return Task.CompletedTask;
            };

            try { await _mqttClient.ConnectAsync(options); }
            catch (Exception ex) { label3.Text = "MQTT Error"; label3.ForeColor = Color.Red; Console.WriteLine($"MQTT: {ex.Message}"); }
        }

        private void ProcessMQTTData(string topic, string payload)
        {
            try
            {
                var json = JsonDocument.Parse(payload);
                var root = json.RootElement;
                int nodeId = root.GetProperty("node").GetInt32();
                float temp = (float)root.GetProperty("temp").GetDouble();
                float hum = (float)root.GetProperty("hum").GetDouble();
                float mcuTemp = root.TryGetProperty("mcu_temp", out var mt) ? (float)mt.GetDouble() : sensorData[nodeId - 1][4];
                float volt = root.TryGetProperty("volt", out var vt) ? (float)vt.GetDouble() : sensorData[nodeId - 1][5];

                int idx = nodeId - 1;
                if (idx < 0 || idx >= 4) return;

                sensorData[idx][0] = temp;
                sensorData[idx][1] = hum;
                sensorData[idx][4] = mcuTemp;
                sensorData[idx][5] = volt;
                deviceData[idx][3] = $"{mcuTemp:F2}";
                deviceData[idx][4] = $"{volt:F2}";
                deviceData[idx][9] = "Just now";
                deviceData[idx][10] = "Online";

                tempHistory[idx].Add(temp);
                humHistory[idx].Add(hum);
                mcuTempHistory[idx].Add(mcuTemp);
                voltHistory[idx].Add(volt);
                if (tempHistory[idx].Count > MAX_HISTORY) tempHistory[idx].RemoveAt(0);
                if (humHistory[idx].Count > MAX_HISTORY) humHistory[idx].RemoveAt(0);
                if (mcuTempHistory[idx].Count > MAX_HISTORY) mcuTempHistory[idx].RemoveAt(0);
                if (voltHistory[idx].Count > MAX_HISTORY) voltHistory[idx].RemoveAt(0);

                if (panelOverview.Visible) LoadOverviewData();
                if (panelDevices.Visible && comboBox1.SelectedIndex == idx)
                {
                    LoadDeviceInfo(idx);
                    LoadSensorInfo(idx);
                    DrawAllCharts(idx);
                }
            }
            catch (Exception ex) { Console.WriteLine($"Parse error: {ex.Message}"); }
        }

        // ==================== TAB SWITCHING ====================
        private void ShowTab(string tab)
        {
            panelOverview.Visible = (tab == "overview");
            panelDevices.Visible = (tab == "devices");
        }

        private void HighlightButton(Button activeBtn)
        {
            var sc = Color.DarkSlateGray; var ac = Color.FromArgb(60, 80, 95);
            btnOverview.BackColor = sc; btnDevices.BackColor = sc; btnMetrics.BackColor = sc; btnAlerts.BackColor = sc; btnSettings.BackColor = sc;
            activeBtn.BackColor = ac;
        }

        private void SetupSidebar()
        {
            btnOverview.BackColor = Color.FromArgb(60, 80, 95);
            btnOverview.Click += btnOverview_Click;
            btnDevices.Click += btnDevices_Click;
            btnMetrics.Click += btnMetrics_Click;
            btnAlerts.Click += btnAlerts_Click;
            btnSettings.Click += btnSettings_Click;
        }

        private void btnOverview_Click(object? sender, EventArgs e) { lblPageTitle.Text = "Overview"; lblPageSubtitle.Text = "BLE Mesh — 4 ESP32 nodes"; HighlightButton(btnOverview); ShowTab("overview"); LoadOverviewData(); }
        private void btnDevices_Click(object? sender, EventArgs e) { lblPageTitle.Text = "Devices"; lblPageSubtitle.Text = "Device details and sensor history"; HighlightButton(btnDevices); ShowTab("devices"); }
        private void btnMetrics_Click(object? sender, EventArgs e) { lblPageTitle.Text = "Metrics"; lblPageSubtitle.Text = "Sensor data over time"; HighlightButton(btnMetrics); }
        private void btnAlerts_Click(object? sender, EventArgs e) { lblPageTitle.Text = "Alerts"; lblPageSubtitle.Text = "System notifications"; HighlightButton(btnAlerts); }
        private void btnSettings_Click(object? sender, EventArgs e) { lblPageTitle.Text = "Settings"; lblPageSubtitle.Text = "Configure your system"; HighlightButton(btnSettings); }

        // ==================== OVERVIEW TAB ====================
        private void LoadOverviewData()
        {
            float avgTemp = sensorData.Average(s => s[0]);
            float avgHum = sensorData.Average(s => s[1]);
            float avgVoc = sensorData.Average(s => s[2]);
            float avgAqi = sensorData.Average(s => s[3]);

            lblAvgTemp.Text = $"{avgTemp:F2}°C";
            lblAvgHum.Text = $"{avgHum:F2}%";
            lblAvgVoc.Text = $"{avgVoc:F2} ppb";
            lblAvgAqi.Text = $"{avgAqi:F2}";

            lblC1Temp.Text = $"{sensorData[0][0]:F2}°C"; lblC1Temp.ForeColor = Color.OrangeRed;
            lblC1Hum.Text = $"{sensorData[0][1]:F2}%"; lblC1Hum.ForeColor = Color.DodgerBlue;
            lblC1Voc.Text = $"{sensorData[0][2]:F2} ppb"; lblC1Voc.ForeColor = Color.DarkGoldenrod;
            lblC1Aqi.Text = $"{sensorData[0][3]:F2}"; lblC1Aqi.ForeColor = Color.Green;

            label41.Text = $"{sensorData[1][0]:F2}°C"; label41.ForeColor = Color.OrangeRed;
            label39.Text = $"{sensorData[1][1]:F2}%"; label39.ForeColor = Color.DodgerBlue;
            label37.Text = $"{sensorData[1][2]:F2} ppb"; label37.ForeColor = Color.DarkGoldenrod;
            label34.Text = $"{sensorData[1][3]:F2}"; label34.ForeColor = Color.Green;

            label50.Text = $"{sensorData[2][0]:F2}°C"; label50.ForeColor = Color.OrangeRed;
            label48.Text = $"{sensorData[2][1]:F2}%"; label48.ForeColor = Color.DodgerBlue;
            label46.Text = $"{sensorData[2][2]:F2} ppb"; label46.ForeColor = Color.DarkGoldenrod;
            label44.Text = $"{sensorData[2][3]:F2}"; label44.ForeColor = Color.Green;

            label59.Text = $"{sensorData[3][0]:F2}°C"; label59.ForeColor = Color.OrangeRed;
            label57.Text = $"{sensorData[3][1]:F2}%"; label57.ForeColor = Color.DodgerBlue;
            label55.Text = $"{sensorData[3][2]:F2} ppb"; label55.ForeColor = Color.DarkGoldenrod;
            label53.Text = $"{sensorData[3][3]:F2}"; label53.ForeColor = Color.Green;

            LoadDeviceTable();
        }

        private void LoadDeviceTable()
        {
            var dt = new DataTable();
            dt.Columns.Add("Device"); dt.Columns.Add("MAC"); dt.Columns.Add("Position");
            dt.Columns.Add("MCU Temp"); dt.Columns.Add("Voltage"); dt.Columns.Add("Current"); dt.Columns.Add("Status");
            for (int i = 0; i < 4; i++) { var d = deviceData[i]; dt.Rows.Add(d[0], d[1], d[2], d[3] + "°C", d[4] + "V", d[5] + "mA", d[10]); }
            dgvDevices.DataSource = dt;

            dgvDevices.EnableHeadersVisualStyles = false;
            dgvDevices.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(245, 245, 245);
            dgvDevices.ColumnHeadersDefaultCellStyle.ForeColor = Color.Gray;
            dgvDevices.ColumnHeadersDefaultCellStyle.Font = new Font("Segoe UI", 9, FontStyle.Bold);
            dgvDevices.ColumnHeadersDefaultCellStyle.SelectionBackColor = Color.FromArgb(245, 245, 245);
            dgvDevices.ColumnHeadersDefaultCellStyle.SelectionForeColor = Color.Gray;
            dgvDevices.ColumnHeadersBorderStyle = DataGridViewHeaderBorderStyle.Single;
            dgvDevices.ColumnHeadersHeight = 35;
            dgvDevices.DefaultCellStyle.BackColor = Color.White;
            dgvDevices.DefaultCellStyle.ForeColor = Color.Black;
            dgvDevices.DefaultCellStyle.SelectionBackColor = Color.White;
            dgvDevices.DefaultCellStyle.SelectionForeColor = Color.Black;
            dgvDevices.DefaultCellStyle.Font = new Font("Segoe UI", 9);
            dgvDevices.GridColor = Color.FromArgb(230, 230, 230);
            dgvDevices.BorderStyle = BorderStyle.None;
            dgvDevices.CellBorderStyle = DataGridViewCellBorderStyle.SingleHorizontal;
            dgvDevices.RowHeadersVisible = false;
            dgvDevices.RowTemplate.Height = 35;
            foreach (DataGridViewRow row in dgvDevices.Rows)
            {
                var sc = row.Cells["Status"];
                if (sc.Value?.ToString() == "Online") sc.Style.ForeColor = Color.Green;
                else sc.Style.ForeColor = Color.Orange;
            }
        }

        // ==================== DEVICES TAB ====================
        private void LoadComboBox()
        {
            comboBox1.Items.Clear();
            comboBox1.Items.Add("Node 1 — ESP32_Node_1 (Corner 1)");
            comboBox1.Items.Add("Node 2 — ESP32_Node_2 (Corner 2)");
            comboBox1.Items.Add("Node 3 — ESP32_Node_3 (Corner 3)");
            comboBox1.Items.Add("Node 4 — ESP32_Node_4 (Corner 4)");
            comboBox1.SelectedIndex = 0;
        }

        private void comboBox1_SelectedIndexChanged(object sender, EventArgs e)
        {
            int i = comboBox1.SelectedIndex;
            if (i < 0) return;
            LoadDeviceInfo(i);
            LoadSensorInfo(i);
            LoadBLEInfo(i);
            DrawAllCharts(i);
        }

        private void LoadDeviceInfo(int i)
        {
            var d = deviceData[i];
            lblDevName.Text = d[0]; lblDevMac.Text = d[1]; lblDevPos.Text = d[2];
            lblDevMcu.Text = $"{sensorData[i][4]:F2}°C";
            lblDevVolt.Text = $"{sensorData[i][5]:F2}V";
            lblDevAmp.Text = d[5] + "mA";
            lblDevUptime.Text = d[6]; lblDevStatus.Text = d[10];
            lblDevStatus.ForeColor = d[10] == "Online" ? Color.Green : Color.Orange;
        }

        private void LoadSensorInfo(int i)
        {
            var s = sensorData[i];
            lblSenTemp.Text = $"{s[0]:F2}°C"; lblSenTemp.ForeColor = Color.OrangeRed; lblSenTemp.Font = new Font("Segoe UI", 18, FontStyle.Bold);
            lblSenHum.Text = $"{s[1]:F2}%"; lblSenHum.ForeColor = Color.DodgerBlue; lblSenHum.Font = new Font("Segoe UI", 18, FontStyle.Bold);
            lblSenVoc.Text = $"{s[2]:F2} ppb"; lblSenVoc.ForeColor = Color.DarkGoldenrod; lblSenVoc.Font = new Font("Segoe UI", 18, FontStyle.Bold);
            lblSenAqi.Text = $"{s[3]:F2}"; lblSenAqi.ForeColor = Color.Green; lblSenAqi.Font = new Font("Segoe UI", 18, FontStyle.Bold);
        }

        private void LoadBLEInfo(int i) { var d = deviceData[i]; lblRssi.Text = d[7]; lblRole.Text = d[8]; lblLastData.Text = d[9]; }

        // ==================== DRAW CHARTS ====================
        private void DrawAllCharts(int idx)
        {
            // Chart 1: Temp (cam) + Hum (xanh) — 2 đường chung 1 chart
            picChartTemp.Image = DrawDualLineChart(
                tempHistory[idx], "°C", Color.OrangeRed,
                humHistory[idx], "%", Color.DodgerBlue,
                "Temp", "Hum");

            // Chart 2: MCU Temperature
            picChartHum.Image = DrawSingleRealtimeChart(
                mcuTempHistory[idx], "°C", Color.FromArgb(180, 60, 60), "MCU Temp");

            // Chart 3: Voltage
            picChartVoc.Image = DrawSingleRealtimeChart(
                voltHistory[idx], "V", Color.FromArgb(0, 120, 200), "Voltage");

            // Chart 4: VOC + AQI (giả lập)
            picChartAqi.Image = DrawSimulatedDualChart(
                sensorData[idx][2], 15f, " ppb", Color.DarkGoldenrod,
                sensorData[idx][3], 8f, "", Color.Green,
                "VOC", "AQI");
        }

        // ==================== CHART: 2 đường realtime ====================
        private Bitmap DrawDualLineChart(List<float> data1, string unit1, Color color1,
                                          List<float> data2, string unit2, Color color2,
                                          string label1, string label2)
        {
            int w = 520, h = 180;
            var bmp = new Bitmap(w, h);
            using var g = Graphics.FromImage(bmp);
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            g.Clear(Color.White);

            if (data1.Count < 2 || data2.Count < 2) return bmp;

            float[] d1 = data1.ToArray();
            float[] d2 = data2.ToArray();
            int count = Math.Min(d1.Length, d2.Length);

            // Trục Y trái (data1)
            float min1 = d1.Min() - 1f, max1 = d1.Max() + 1f;
            if (max1 - min1 < 2f) { min1 -= 1f; max1 += 1f; }
            float range1 = max1 - min1;

            // Trục Y phải (data2)
            float min2 = d2.Min() - 1f, max2 = d2.Max() + 1f;
            if (max2 - min2 < 2f) { min2 -= 1f; max2 += 1f; }
            float range2 = max2 - min2;

            int padLeft = 55, padRight = 55, padTop = 25, padBottom = 25;
            int chartW = w - padLeft - padRight;
            int chartH = h - padTop - padBottom;

            // Legend
            using var legendFont = new Font("Segoe UI", 8, FontStyle.Bold);
            using var brush1 = new SolidBrush(color1);
            using var brush2 = new SolidBrush(color2);
            g.FillRectangle(brush1, padLeft, 5, 10, 10);
            g.DrawString(label1, legendFont, brush1, padLeft + 14, 3);
            g.FillRectangle(brush2, padLeft + 80, 5, 10, 10);
            g.DrawString(label2, legendFont, brush2, padLeft + 94, 3);

            // Grid
            using var gridPen = new Pen(Color.FromArgb(230, 230, 230), 1);
            using var axisFont = new Font("Segoe UI", 7);
            using var axisBrush1 = new SolidBrush(color1);
            using var axisBrush2 = new SolidBrush(color2);
            for (int i = 0; i <= 4; i++)
            {
                int y = padTop + (int)(chartH * i / 4.0f);
                g.DrawLine(gridPen, padLeft, y, w - padRight, y);
                float v1 = max1 - (range1 * i / 4.0f);
                float v2 = max2 - (range2 * i / 4.0f);
                g.DrawString($"{v1:F1}{unit1}", axisFont, axisBrush1, 0, y - 6);
                g.DrawString($"{v2:F1}{unit2}", axisFont, axisBrush2, w - 52, y - 6);
            }

            // X labels
            int labelCount = Math.Min(7, count);
            using var xBrush = new SolidBrush(Color.Gray);
            for (int i = 0; i < labelCount; i++)
            {
                int dataIdx = (int)((count - 1) * i / (float)(labelCount - 1));
                int x = padLeft + (int)(chartW * dataIdx / (float)(count - 1));
                int secAgo = (count - 1 - dataIdx) * 3;
                string lbl = secAgo == 0 ? "now" : $"-{secAgo}s";
                g.DrawString(lbl, axisFont, xBrush, x - 10, h - 16);
            }

            // Line 1 + fill
            var pts1 = new List<PointF>();
            for (int i = 0; i < count; i++)
            {
                float x = padLeft + chartW * i / (float)(count - 1);
                float y = padTop + chartH * (1 - (d1[i] - min1) / range1);
                pts1.Add(new PointF(x, y));
            }
            var fill1 = new List<PointF>(pts1);
            fill1.Add(new PointF(padLeft + chartW, padTop + chartH));
            fill1.Add(new PointF(padLeft, padTop + chartH));
            using var fb1 = new SolidBrush(Color.FromArgb(20, color1));
            g.FillPolygon(fb1, fill1.ToArray());
            using var lp1 = new Pen(color1, 2);
            for (int i = 0; i < count - 1; i++) g.DrawLine(lp1, pts1[i], pts1[i + 1]);

            // Line 2
            var pts2 = new List<PointF>();
            for (int i = 0; i < count; i++)
            {
                float x = padLeft + chartW * i / (float)(count - 1);
                float y = padTop + chartH * (1 - (d2[i] - min2) / range2);
                pts2.Add(new PointF(x, y));
            }
            using var lp2 = new Pen(color2, 2) { DashStyle = System.Drawing.Drawing2D.DashStyle.Dash };
            for (int i = 0; i < count - 1; i++) g.DrawLine(lp2, pts2[i], pts2[i + 1]);

            // Dots hiện tại
            g.FillEllipse(brush1, pts1[count - 1].X - 4, pts1[count - 1].Y - 4, 8, 8);
            g.FillEllipse(brush2, pts2[count - 1].X - 4, pts2[count - 1].Y - 4, 8, 8);

            return bmp;
        }

        // ==================== CHART: 1 đường realtime ====================
        private Bitmap DrawSingleRealtimeChart(List<float> history, string unit, Color lineColor, string chartLabel)
        {
            int w = 520, h = 180;
            var bmp = new Bitmap(w, h);
            using var g = Graphics.FromImage(bmp);
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            g.Clear(Color.White);

            if (history.Count < 2) return bmp;

            float[] data = history.ToArray();
            int count = data.Length;

            float minVal = data.Min() - 0.5f, maxVal = data.Max() + 0.5f;
            if (maxVal - minVal < 1f) { minVal -= 0.5f; maxVal += 0.5f; }
            float range = maxVal - minVal;

            int padLeft = 55, padRight = 15, padTop = 25, padBottom = 25;
            int chartW = w - padLeft - padRight;
            int chartH = h - padTop - padBottom;

            // Label
            using var labelFont = new Font("Segoe UI", 8, FontStyle.Bold);
            using var labelBrush = new SolidBrush(lineColor);
            g.FillRectangle(labelBrush, padLeft, 5, 10, 10);
            g.DrawString(chartLabel, labelFont, labelBrush, padLeft + 14, 3);

            // Grid
            using var gridPen = new Pen(Color.FromArgb(230, 230, 230), 1);
            using var axisFont = new Font("Segoe UI", 7);
            using var axisBrush = new SolidBrush(Color.Gray);
            for (int i = 0; i <= 4; i++)
            {
                int y = padTop + (int)(chartH * i / 4.0f);
                g.DrawLine(gridPen, padLeft, y, w - padRight, y);
                float v = maxVal - (range * i / 4.0f);
                g.DrawString($"{v:F2}{unit}", axisFont, axisBrush, 0, y - 6);
            }

            // X labels
            int lc = Math.Min(7, count);
            for (int i = 0; i < lc; i++)
            {
                int di = (int)((count - 1) * i / (float)(lc - 1));
                int x = padLeft + (int)(chartW * di / (float)(count - 1));
                int sa = (count - 1 - di) * 3;
                g.DrawString(sa == 0 ? "now" : $"-{sa}s", axisFont, axisBrush, x - 10, h - 16);
            }

            // Fill + Line
            var pts = new List<PointF>();
            for (int i = 0; i < count; i++)
            {
                float x = padLeft + chartW * i / (float)(count - 1);
                float y = padTop + chartH * (1 - (data[i] - minVal) / range);
                pts.Add(new PointF(x, y));
            }
            var fill = new List<PointF>(pts);
            fill.Add(new PointF(padLeft + chartW, padTop + chartH));
            fill.Add(new PointF(padLeft, padTop + chartH));
            using var fillBrush = new SolidBrush(Color.FromArgb(25, lineColor));
            g.FillPolygon(fillBrush, fill.ToArray());
            using var linePen = new Pen(lineColor, 2);
            for (int i = 0; i < count - 1; i++) g.DrawLine(linePen, pts[i], pts[i + 1]);

            // Dot + value
            var last = pts[count - 1];
            using var dotBrush = new SolidBrush(lineColor);
            g.FillEllipse(dotBrush, last.X - 4, last.Y - 4, 8, 8);
            using var valFont = new Font("Segoe UI", 9, FontStyle.Bold);
            g.DrawString($"{data[count - 1]:F2}{unit}", valFont, dotBrush, last.X - 55, last.Y - 18);

            return bmp;
        }

        // ==================== CHART: 2 đường giả lập (VOC+AQI) ====================
        private Bitmap DrawSimulatedDualChart(float base1, float var1, string unit1, Color color1,
                                               float base2, float var2, string unit2, Color color2,
                                               string label1, string label2)
        {
            int w = 520, h = 180;
            var bmp = new Bitmap(w, h);
            using var g = Graphics.FromImage(bmp);
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            g.Clear(Color.White);

            var rand = new Random();
            float[] d1 = new float[14], d2 = new float[14];
            float v1 = base1, v2 = base2;
            for (int i = 0; i < 14; i++)
            {
                v1 += (float)(rand.NextDouble() - 0.5) * var1;
                v1 = Math.Max(base1 - var1 * 2, Math.Min(base1 + var1 * 2, v1));
                d1[i] = v1;
                v2 += (float)(rand.NextDouble() - 0.5) * var2;
                v2 = Math.Max(base2 - var2 * 2, Math.Min(base2 + var2 * 2, v2));
                d2[i] = v2;
            }

            float min1 = d1.Min() - var1, max1 = d1.Max() + var1, range1 = max1 - min1;
            float min2 = d2.Min() - var2, max2 = d2.Max() + var2, range2 = max2 - min2;

            int padLeft = 55, padRight = 45, padTop = 25, padBottom = 25;
            int chartW = w - padLeft - padRight;
            int chartH = h - padTop - padBottom;

            // Legend
            using var lf = new Font("Segoe UI", 8, FontStyle.Bold);
            using var b1 = new SolidBrush(color1);
            using var b2 = new SolidBrush(color2);
            g.FillRectangle(b1, padLeft, 5, 10, 10);
            g.DrawString(label1, lf, b1, padLeft + 14, 3);
            g.FillRectangle(b2, padLeft + 80, 5, 10, 10);
            g.DrawString(label2, lf, b2, padLeft + 94, 3);

            // Grid
            using var gp = new Pen(Color.FromArgb(230, 230, 230), 1);
            using var af = new Font("Segoe UI", 7);
            using var ab1 = new SolidBrush(color1);
            using var ab2 = new SolidBrush(color2);
            for (int i = 0; i <= 4; i++)
            {
                int y = padTop + (int)(chartH * i / 4.0f);
                g.DrawLine(gp, padLeft, y, w - padRight, y);
                g.DrawString($"{(max1 - range1 * i / 4f):F0}{unit1}", af, ab1, 0, y - 6);
                g.DrawString($"{(max2 - range2 * i / 4f):F0}{unit2}", af, ab2, w - 42, y - 6);
            }

            // X labels
            using var xb = new SolidBrush(Color.Gray);
            string[] xlbl = { "3/14", "3/16", "3/18", "3/20", "3/22", "3/24", "3/27" };
            for (int i = 0; i < xlbl.Length; i++)
            {
                int x = padLeft + (int)(chartW * i / (float)(xlbl.Length - 1));
                g.DrawString(xlbl[i], af, xb, x - 10, h - 16);
            }

            // Line 1 + fill
            var pts1 = new List<PointF>();
            for (int i = 0; i < 14; i++) pts1.Add(new PointF(padLeft + chartW * i / 13f, padTop + chartH * (1 - (d1[i] - min1) / range1)));
            var f1 = new List<PointF>(pts1); f1.Add(new PointF(padLeft + chartW, padTop + chartH)); f1.Add(new PointF(padLeft, padTop + chartH));
            using var fb1 = new SolidBrush(Color.FromArgb(20, color1));
            g.FillPolygon(fb1, f1.ToArray());
            using var lp1 = new Pen(color1, 2);
            for (int i = 0; i < 13; i++) g.DrawLine(lp1, pts1[i], pts1[i + 1]);

            // Line 2
            var pts2 = new List<PointF>();
            for (int i = 0; i < 14; i++) pts2.Add(new PointF(padLeft + chartW * i / 13f, padTop + chartH * (1 - (d2[i] - min2) / range2)));
            using var lp2 = new Pen(color2, 2) { DashStyle = System.Drawing.Drawing2D.DashStyle.Dash };
            for (int i = 0; i < 13; i++) g.DrawLine(lp2, pts2[i], pts2[i + 1]);

            return bmp;
        }

        // ==================== EMPTY EVENTS ====================
        private void panelSidebar_Paint(object sender, PaintEventArgs e) { }
        private void panel1_Paint(object sender, PaintEventArgs e) { }
        private void label1_Click(object sender, EventArgs e) { }
        private void label1_Click_1(object sender, EventArgs e) { }
        private void pictureBox1_Click(object sender, EventArgs e) { }
        private void panelContent_Paint(object sender, PaintEventArgs e) { }
        private void label2_Click(object sender, EventArgs e) { }
        private void lblPageSubtitle_Click(object sender, EventArgs e) { }
        private void lblPageTitle_Click(object sender, EventArgs e) { }
        private void label9_Click(object sender, EventArgs e) { }
        private void label13_Click(object sender, EventArgs e) { }
        private void label10_Click(object sender, EventArgs e) { }
        private void lblSenTemp_Click(object sender, EventArgs e) { }
        private void label12_Click(object sender, EventArgs e) { }
        private void lblSenAqi_Click(object sender, EventArgs e) { }
        private void label14_Click(object sender, EventArgs e) { }
        private void label16_Click(object sender, EventArgs e) { }
        private void label26_Click(object sender, EventArgs e) { }
        private void label33_Click(object sender, EventArgs e) { }
        private void btnSettings_Click_1(object sender, EventArgs e) { }
    }
}