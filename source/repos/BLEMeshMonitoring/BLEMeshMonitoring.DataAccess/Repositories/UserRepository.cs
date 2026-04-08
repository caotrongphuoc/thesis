using BLEMeshMonitoring.DataAccess.Context;
using BLEMeshMonitoring.DataAccess.Models;

namespace BLEMeshMonitoring.DataAccess.Repositories
{
    public class UserRepository
    {
        public User? Login(string username, string password)
        {
            using var db = new AppDbContext();
            return db.Users.FirstOrDefault(u =>
                u.Username == username && u.Password == password);
        }

        public bool Register(string username, string password)
        {
            using var db = new AppDbContext();
            if (db.Users.Any(u => u.Username == username))
                return false;

            db.Users.Add(new User
            {
                Username = username,
                Password = password
            });
            db.SaveChanges();
            return true;
        }

        public List<User> GetAllUsers()
        {
            using var db = new AppDbContext();
            return db.Users.ToList();
        }
    }
}