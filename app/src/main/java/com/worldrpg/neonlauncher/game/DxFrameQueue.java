package com.worldrpg.neonlauncher.game;

/** Keeps offscreen drawing and resource releases ordered when UI frames are replaced. */
final class DxFrameQueue {
    private final int budget;
    private final StringBuilder carried = new StringBuilder();
    private String frame = "";
    private boolean drawn = true;

    DxFrameQueue(int budget) { this.budget = budget; }

    void offer(String next) {
        if (!drawn) retainPersistent(frame);
        frame = next == null ? "" : next;
        drawn = false;
    }

    boolean hasCommands() { return !frame.isEmpty() || carried.length() != 0; }

    String take() {
        drawn = true;
        // Each native frame starts with default state unless it explicitly sets
        // another mode. A retained target must not change the newest screen frame.
        String result = carried.length() == 0 ? frame : carried + "G\nB 0\nF\n" + frame;
        carried.setLength(0);
        return result;
    }

    private void retainSection(StringBuilder section) {
        if (section == null) return;
        section.append("G\n");
        if (carried.length() + section.length() <= budget) carried.append(section);
    }

    private void retainPersistent(String commands) {
        String blend = "B 0", font = "F";
        StringBuilder section = null;
        for (String line : commands.split("\n")) {
            if (line.startsWith("B ")) blend = line;
            if (line.equals("F") || line.startsWith("F ")) font = line;
            if (line.startsWith("G ")) {
                retainSection(section);
                section = new StringBuilder().append(blend).append('\n').append(font).append('\n').append(line).append('\n');
            } else if (line.equals("G")) {
                retainSection(section);
                section = null;
            } else if (line.startsWith("D ")) {
                retainSection(section);
                section = null;
                // Releases are never dropped with screen frames or the draw budget.
                // Execute them after older target commands, never before them.
                carried.append(line).append('\n');
            } else if (section != null) section.append(line).append('\n');
        }
        retainSection(section);
    }
}
