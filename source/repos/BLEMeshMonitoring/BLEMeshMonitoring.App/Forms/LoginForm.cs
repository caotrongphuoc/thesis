using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace BLEMeshMonitoring.App.Forms
{
    public partial class LoginForm : Form
    {

        private bool dragging = false;
        private Point dragCursorPoint;
        private Point dragFormPoint;

        public LoginForm()
        {
            InitializeComponent();
            this.FormBorderStyle = FormBorderStyle.None;
            this.DoubleBuffered = true;
            this.Paint += LoginForm_Paint;
            this.Resize += (s, e) => this.Invalidate();
        }

        protected override void OnLoad(EventArgs e)
        {
            base.OnLoad(e);
            SetRoundRegion();
        }

        private void SetRoundRegion()
        {
            GraphicsPath path = new GraphicsPath();
            int r = 40;
            path.AddArc(0, 0, r, r, 180, 90);
            path.AddArc(this.Width - r - 1, 0, r, r, 270, 90);
            path.AddArc(this.Width - r - 1, this.Height - r - 1, r, r, 0, 90);
            path.AddArc(0, this.Height - r - 1, r, r, 90, 90);
            path.CloseFigure();
            this.Region = new Region(path);
        }

        private void LoginForm_Paint(object sender, PaintEventArgs e)
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        }

        private void label1_Click(object sender, EventArgs e)
        {

        }

        private void label2_Click(object sender, EventArgs e)
        {

        }

        private void label3_Click(object sender, EventArgs e)
        {

        }

        private void label4_Click(object sender, EventArgs e)
        {

        }

        private void button1_Click(object sender, EventArgs e)
        {
            var authService = new BLEMeshMonitoring.Logic.Services.AuthService();
            var user = authService.Login(textBox1.Text, textBox2.Text);
            if (user != null)
            {
                var dashboard = new DashBoard();
                dashboard.Show();
                this.Hide();
            }
            else
            {
                MessageBox.Show("Wrong username or password!", "Error",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void linkLabel1_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
        {

        }

        private void LoginForm_Load(object sender, EventArgs e)
        {

        }

        private void LoginForm_MouseDown(object sender, MouseEventArgs e)
        {
            dragging = true;
            dragCursorPoint = Cursor.Position;
            dragFormPoint = this.Location;
        }

        private void LoginForm_MouseMove(object sender, MouseEventArgs e)
        {
            if (dragging)
            {
                Point diff = Point.Subtract(Cursor.Position, new Size(dragCursorPoint));
                this.Location = Point.Add(dragFormPoint, new Size(diff));
            }
        }

        private void LoginForm_MouseUp(object sender, MouseEventArgs e)
        {
            dragging = false;
        }
    }
}
