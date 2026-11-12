package com.worldrpg.neonlauncher.network

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Manages native connection state and exposes it as a Flow.
 * Actual networking is handled in native C++ via JNI.
 */
class ConnectionManager {

    sealed class ConnectionState {
        object Disconnected : ConnectionState()
        object Connecting : ConnectionState()
        data class Connected(val serverIp: String, val serverPort: Int) : ConnectionState()
        data class Error(val message: String) : ConnectionState()
    }

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    /**
     * Initiates connection to a server via native layer.
     * Called from GameActivity after nativeInit().
     */
    fun setConnecting(ip: String, port: Int) {
        _connectionState.value = ConnectionState.Connecting
    }

    /**
     * Marks connection as established (called from JNI callback).
     */
    fun setConnected(ip: String, port: Int) {
        _connectionState.value = ConnectionState.Connected(ip, port)
    }

    /**
     * Marks connection as disconnected.
     */
    fun setDisconnected() {
        _connectionState.value = ConnectionState.Disconnected
    }

    /**
     * Marks connection error state.
     */
    fun setError(message: String) {
        _connectionState.value = ConnectionState.Error(message)
    }

    /**
     * Returns true if currently connected or connecting.
     */
    fun isActive(): Boolean {
        return when (_connectionState.value) {
            is ConnectionState.Connected, is ConnectionState.Connecting -> true
            else -> false
        }
    }
}