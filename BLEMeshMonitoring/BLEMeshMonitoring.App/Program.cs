using BLEMeshMonitoring.App.Forms;
using BLEMeshMonitoring.DataAccess.Context;

namespace BLEMeshMonitoring.App
{
    internal static class Program
    {
        [STAThread]
        static void Main()
        {
            // Tạo database nếu chưa có
            using (var db = new AppDbContext())
            {
                db.Database.EnsureCreated();
            }

            ApplicationConfiguration.Initialize();
            Application.Run(new LoginForm());
        }
    }
}