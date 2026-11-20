package com.worldrpg.neonlauncher.ui

/**
 * Nickname checks of the desktop client:
 * CCore::IsValidNick (Client/core/CCore.cpp:3041-3063): 1..22 characters, each 33..126.
 * CConnectManager::CheckNickProvided (Client/core/CConnectManager.cpp:780-791):
 * not admin/console/server (any case), no backtick.
 */
object NicknameRules {
    const val MAX_LENGTH = 22

    enum class Result { OK, EMPTY, INVALID, RESERVED }

    fun check(nick: String): Result {
        if (nick.isEmpty()) return Result.EMPTY
        if (nick.length > MAX_LENGTH || nick.any { it.code < 33 || it.code > 126 }) return Result.INVALID
        val lower = nick.lowercase()
        if (lower == "admin" || lower == "console" || lower == "server" || '`' in nick) return Result.RESERVED
        return Result.OK
    }
}
