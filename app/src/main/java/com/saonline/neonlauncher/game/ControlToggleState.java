package com.saonline.neonlauncher.game;

/**
 * The two mod commands the activity itself enforces. Native shared/ControlToggles.cpp tells the activity about
 * every change as game line kind 20, "radar|0/1" and "chatbox|0/1" (CKeyBinds::SetControlEnabled on the mod
 * commands "radar" and "chatbox", CClient.cpp:66-72). Every GTA control is enforced natively in the CPad
 * getters (gtasa/ControlMask.cpp), so it is not tracked here.
 */
final class ControlToggleState {
    private volatile boolean radar = true;
    private volatile boolean chatbox = true;

    /** F11 and a tap on the radar open the player map only while this is true. */
    boolean radarEnabled() {
        return radar;
    }

    /** T, "/", the Chat button and a tap on the chat log open the chat input only while this is true. */
    boolean chatboxEnabled() {
        return chatbox;
    }

    /** Applies one kind-20 payload. Returns false for a name this class does not track. */
    boolean apply(String line) {
        if (line == null) return false;
        int bar = line.indexOf('|');
        if (bar <= 0 || bar + 1 >= line.length()) return false;
        boolean enabled = line.charAt(bar + 1) != '0';
        String name = line.substring(0, bar);
        if (name.equals("radar")) {
            radar = enabled;
            return true;
        }
        if (name.equals("chatbox")) {
            chatbox = enabled;
            return true;
        }
        return false;
    }

    /** CClientGame.cpp:586 and ResetMapInfo (6429): SetAllControlsEnabled(true, true, true). */
    void reset() {
        radar = true;
        chatbox = true;
    }
}
