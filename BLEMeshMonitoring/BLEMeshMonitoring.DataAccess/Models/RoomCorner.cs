namespace BLEMeshMonitoring.DataAccess.Models
{
    public class RoomCorner
    {
        public int Id { get; set; }
        public string CornerName { get; set; } = string.Empty;  // "Góc 1", "Góc 2"...
        public int ESP32DeviceId { get; set; }
        public string Description { get; set; } = string.Empty; // "Góc trên trái phòng"
    }
}