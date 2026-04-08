using BLEMeshMonitoring.DataAccess.Models;
using BLEMeshMonitoring.DataAccess.Repositories;

namespace BLEMeshMonitoring.Logic.Services
{
    public class AuthService
    {
        private readonly UserRepository _userRepo = new();

        public User? Login(string username, string password)
        {
            if (string.IsNullOrWhiteSpace(username) || string.IsNullOrWhiteSpace(password))
                return null;

            return _userRepo.Login(username, password);
        }

        public bool Register(string username, string password)
        {
            if (string.IsNullOrWhiteSpace(username) || password.Length < 4)
                return false;

            return _userRepo.Register(username, password);
        }
    }
}