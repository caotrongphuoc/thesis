using BLEMeshMonitoring.DataAccess.Models;
using BLEMeshMonitoring.DataAccess.Repositories;

namespace BLEMeshMonitoring.Logic.Services
{
    public class SensorDataService
    {
        private readonly SensorDataRepository _sensorRepo = new();

        public void RecordSensorData(int deviceId, float temp, float humidity, float voc, float aqi)
        {
            var data = new SensorData
            {
                ESP32DeviceId = deviceId,
                Temperature = temp,
                Humidity = humidity,
                VOC = voc,
                AirQualityIndex = aqi,
                RecordedAt = DateTime.Now
            };
            _sensorRepo.AddSensorData(data);
        }

        public List<SensorData> GetLatestByDevice(int deviceId)
        {
            return _sensorRepo.GetLatestByDevice(deviceId);
        }

        public List<SensorData> GetAllLatestData()
        {
            return _sensorRepo.GetAllLatest();
        }
    }
}