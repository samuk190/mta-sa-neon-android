# WorldRPG Neon Launcher ProGuard Rules

# Keep JNI native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep native library loading
-keep class com.saonline.neonlauncher.game.** { *; }
-keep class com.saonline.neonlauncher.network.** { *; }

# Keep data classes used for serialization
-keep class com.saonline.neonlauncher.network.packets.** { *; }
-keep class com.saonline.neonlauncher.game.entity.** { *; }

# AndroidX
-keep class androidx.** { *; }
-keep interface androidx.** { *; }

# GTA registers and calls these vendor Java classes and methods by name.
-keep class com.nvidia.devtech.** { *; }
-keep class com.wardrumstudios.utils.** { *; }
-keep class com.rockstargames.gtasa.** { *; }
