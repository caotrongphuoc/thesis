namespace BLEMeshMonitoring.DataAccess.Models
{
    public class User
    {
        public int Id { get; set; }
        public string Username { get; set; } = string.Empty;
        public string Password { get; set; } = string.Empty;
        public string Role { get; set; } = "User"; // "Admin" hoặc "User"
        public DateTime CreatedAt { get; set; } = DateTime.Now;
    }
}