package com.saonline.neonlauncher.game;

/** Run with javac/java; the activity wiring itself still requires a device. */
public final class ControlToggleStateTest {
    private static int checks;
    private static void check(boolean value) {
        checks++;
        if (!value) throw new AssertionError("control toggle check " + checks);
    }
    public static void main(String[] args) {
        ControlToggleState state = new ControlToggleState();
        check(state.radarEnabled() && state.chatboxEnabled());
        check(state.apply("radar|0") && !state.radarEnabled() && state.chatboxEnabled());
        check(state.apply("chatbox|0") && !state.chatboxEnabled());
        check(state.apply("radar|1") && state.radarEnabled() && !state.chatboxEnabled());
        check(state.apply("chatbox|1") && state.chatboxEnabled());
        // Native only ever sends 0 or 1, and anything but "0" counts as enabled.
        check(state.apply("radar|0") && state.apply("radar|true") && state.radarEnabled());
        check(!state.apply("voiceptt|0") && state.radarEnabled() && state.chatboxEnabled());
        check(!state.apply("radar") && !state.apply("radar|") && !state.apply("|1") && !state.apply("") && !state.apply(null));
        state.apply("radar|0");
        state.apply("chatbox|0");
        state.reset();
        check(state.radarEnabled() && state.chatboxEnabled());
        System.out.println("control toggle state tests: " + checks + " checks passed");
    }
}
