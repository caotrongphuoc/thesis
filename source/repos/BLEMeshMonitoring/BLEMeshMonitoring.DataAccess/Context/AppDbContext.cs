using Microsoft.EntityFrameworkCore;
using BLEMeshMonitoring.DataAccess.Models;

namespace BLEMeshMonitoring.DataAccess.Context
{
    public class AppDbContext : DbContext
    {
        public DbSet<User> Users { get; set; }
        public DbSet<ESP32Device> ESP32Devices { get; set; }
        public DbSet<SensorData> SensorDatas { get; set; }
        public DbSet<RoomCorner> RoomCorners { get; set; }

        protected override void OnConfiguring(DbContextOptionsBuilder optionsBuilder)
        {
            // Database SQLite sẽ lưu ở thư mục chạy app
            optionsBuilder.UseSqlite("Data Source=BLEMeshMonitoring.db");
        }
    }
}