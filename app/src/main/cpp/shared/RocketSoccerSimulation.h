#pragma once
// Rocket soccer scene over the RocketWeb kernel (rocketweb/README.md), the
// same class as desktop CRocketSoccerSimulation.h. The kernel owns motion;
// scripts render the snapshots.

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace WorldRPG::RocketSoccer {

struct Controls {
    float throttle{}, steer{}, pitch{}, yaw{}, roll{};
    bool jump{}, boost{}, handbrake{};
};

struct Body {
    std::array<float, 3> position{}, forward{}, right{}, up{}, velocity{};
};

struct Snapshot {
    Body car, ball;
    std::vector<Body> cars;
    std::vector<float> raw;
    bool ballOnGround{};
    bool grounded{}, boosting{};
    int goalTeam{};
    unsigned long long ticks{};
};

struct Triangle {
    std::array<float, 9> vertices;
    int material{};
};
const std::vector<Triangle>& GetArenaMesh();

// Each call throws std::runtime_error when the kernel reports a failure.
class Simulation {
public:
    Simulation();
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    Snapshot Step(float seconds, const Controls& controls);
    Snapshot Predict(std::string_view checkpoint, const std::vector<std::array<float, 8>>& frames, int slot,
                     const std::array<float, 8>& opponent, float fraction);
    std::string Rebase(std::string_view checkpoint);
    void Reset(int kickoff = -1);
    void Configure(bool flat, bool opponent);
    void SetControls(int car, const Controls& controls);
    void SetUnlimitedBoost(bool unlimited);
    bool ControlBall(int action);
    int PollGoal();
    std::vector<float> Pads();
    std::array<double, 42> Camera(const std::array<double, 28>& input);
    void ResetCamera();

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

}  // namespace WorldRPG::RocketSoccer
