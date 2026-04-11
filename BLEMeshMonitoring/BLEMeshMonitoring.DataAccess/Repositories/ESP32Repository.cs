using BLEMeshMonitoring.DataAccess.Context;
using BLEMeshMonitoring.DataAccess.Models;

namespace BLEMeshMonitoring.DataAccess.Repositories
{
    public class ESP32Repository
    {
        public List<ESP32Device> GetAllDevices()
        {
            using var db = new AppDbContext();
            return db.ESP32Devices.ToList();
        }

        public ESP32Device? GetDeviceById(int id)
        {
            using var db = new AppDbContext();
            return db.ESP32Devices.FirstOrDefault(d => d.Id == id);
        }

        public void UpdateDevice(ESP32Device device)
        {
            using var db = new AppDbContext();
            db.ESP32Devices.Update(device);
            db.SaveChanges();
        }

        public void AddDevice(ESP32Device device)
        {
            using var db = new AppDbContext();
            db.ESP32Devices.Add(device);
            db.SaveChanges();
        }
    }
}