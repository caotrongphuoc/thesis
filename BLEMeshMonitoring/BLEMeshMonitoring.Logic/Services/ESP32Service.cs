using BLEMeshMonitoring.DataAccess.Models;
using BLEMeshMonitoring.DataAccess.Repositories;

namespace BLEMeshMonitoring.Logic.Services
{
    public class ESP32Service
    {
        private readonly ESP32Repository _espRepo = new();

        public List<ESP32Device> GetAllDevices()
        {
            return _espRepo.GetAllDevices();
        }

        public ESP32Device? GetDeviceById(int id)
        {
            return _espRepo.GetDeviceById(id);
        }

        public void UpdateDeviceStatus(ESP32Device device)
        {
            _espRepo.UpdateDevice(device);
        }

        public void AddDevice(ESP32Device device)
        {
            _espRepo.AddDevice(device);
        }
    }
}