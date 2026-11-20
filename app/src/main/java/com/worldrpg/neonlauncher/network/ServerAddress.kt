package com.worldrpg.neonlauncher.network

import java.util.Locale

/** IPv4/DNS endpoints supported by the Neon transport, normalized for favorite keys. */
data class ServerAddress(val host: String, val port: Int) {
    fun key(): String = "$host:$port"

    companion object {
        fun normalizeHost(input: String): String? {
            val host = input.trim().removeSuffix(".").lowercase(Locale.ROOT)
            if (host.isEmpty() || host.length > 253) return null
            if (host.all { it in '0'..'9' || it == '.' }) {
                val octets = host.split('.')
                if (octets.size != 4 || octets.any { it.isEmpty() || it.length > 3 || it.toIntOrNull() !in 0..255 }) return null
                return octets.joinToString(".") { it.toInt().toString() }
            }
            if (host.split('.').any { label ->
                    label.isEmpty() || label.length > 63 || label.first() == '-' || label.last() == '-' ||
                        label.any { it !in 'a'..'z' && it !in '0'..'9' && it != '-' }
                }) return null
            return host
        }

        fun parsePort(input: String): Int? {
            val value = input.trim()
            if (value.isEmpty() || value.length > 5 || value.any { it !in '0'..'9' }) return null
            return value.toIntOrNull()?.takeIf { it in 1..65535 }
        }

        fun fromKey(key: String): ServerAddress? {
            val separator = key.lastIndexOf(':')
            if (separator < 1) return null
            val host = normalizeHost(key.substring(0, separator)) ?: return null
            val port = parsePort(key.substring(separator + 1)) ?: return null
            return ServerAddress(host, port)
        }
    }
}
