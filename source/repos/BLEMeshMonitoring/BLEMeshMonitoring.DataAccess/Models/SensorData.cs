namespace BLEMeshMonitoring.DataAccess.Models
{
    public class SensorData
    {
        public int Id { get; set; }
        public int ESP32DeviceId { get; set; }          // Liên kết với ESP32 nào
        public float Temperature { get; set; }           // Nhiệt độ môi trường (°C)
        public float Humidity { get; set; }              // Độ ẩm (%)
        public float VOC { get; set; }                   // Khí hợp chất hữu cơ bay hơi
        public float AirQualityIndex { get; set; }       // Chất lượng không khí (AQI)
        public DateTime RecordedAt { get; set; } = DateTime.Now;
    }
}