#pragma once

#include "spider/game_session.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace spider {

enum class LoadLastSessionStatus {
    loaded,
    not_found,
    invalid_data,
};

struct LoadLastSessionResult {
    LoadLastSessionStatus status {LoadLastSessionStatus::not_found};
    std::optional<GameSession> session {};
    std::string message {};
};

[[nodiscard]] auto default_last_session_path() -> std::filesystem::path;

[[nodiscard]] auto load_last_session() -> LoadLastSessionResult;
[[nodiscard]] auto load_last_session(const std::filesystem::path& path) -> LoadLastSessionResult;

[[nodiscard]] auto resume_or_create_session(std::uint64_t new_game_seed) -> GameSession;
[[nodiscard]] auto resume_or_create_session(const std::filesystem::path& path, std::uint64_t new_game_seed) -> GameSession;

void save_last_session(const GameSession& session);
void save_last_session(const std::filesystem::path& path, const GameSession& session);

void clear_last_session();
void clear_last_session(const std::filesystem::path& path);

} // namespace spider
