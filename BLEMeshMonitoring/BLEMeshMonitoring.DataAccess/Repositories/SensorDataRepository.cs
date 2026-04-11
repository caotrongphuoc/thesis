using BLEMeshMonitoring.DataAccess.Context;
using BLEMeshMonitoring.DataAccess.Models;

namespace BLEMeshMonitoring.DataAccess.Repositories
{
    public class SensorDataRepository
    {
        public void AddSensorData(SensorData data)
        {
            using var db = new AppDbContext();
            db.SensorDatas.Add(data);
            db.SaveChanges();
        }

        public List<SensorData> GetLatestByDevice(int deviceId, int count = 10)
        {
            using var db = new AppDbContext();
            return db.SensorDatas
                .Where(s => s.ESP32DeviceId == deviceId)
                .OrderByDescending(s => s.RecordedAt)
                .Take(count)
                .ToList();
        }

        public List<SensorData> GetAllLatest()
        {
            using var db = new AppDbContext();
            return db.SensorDatas
                .GroupBy(s => s.ESP32DeviceId)
                .Select(g => g.OrderByDescending(s => s.RecordedAt).First())
                .ToList();
        }
    }
}