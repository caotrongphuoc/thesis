namespace BLEMeshMonitoring.DataAccess.Models
{
    public class ESP32Device
    {
        public int Id { get; set; }
        public string DeviceName { get; set; } = string.Empty;  // VD: "ESP32_Corner1"
        public string MacAddress { get; set; } = string.Empty;
        public string CornerPosition { get; set; } = string.Empty; // "TopLeft", "TopRight", "BottomLeft", "BottomRight"
        public float DeviceTemperature { get; set; }  // Nhiệt độ ESP32
        public float Voltage { get; set; }             // Nguồn (V)
        public float Ampere { get; set; }              // Dòng điện (A)
        public bool IsOnline { get; set; }
        public DateTime LastSeen { get; set; }
    }
}