package com.saonline.neonlauncher.game;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;
import com.saonline.neonlauncher.R;

/**
 * In-game settings. The graphics, audio and some gameplay rows drive GTA SA mobile's own options table
 * (native MobileSettings::settings, see gtasa/MobileSettings.h). Choices are kept in this app's
 * preferences and applied again every time the player joins, because the game reloads its own file
 * at boot. The client rows (chat) are handled by the activity.
 */
public final class GameSettingsView extends FrameLayout {

    public interface Host {
        /** {value, min, max} of a GTA option, or null when the table is not available. */
        int[] get(int index);
        boolean set(int index, int value);
        void setChatShown(boolean shown);
        void setHudLayout(int chatAnchor, int buttonsAnchor, int chatPercent, boolean chatTouch);
        int getInterfaceScalePercent();
        boolean setInterfaceScalePercent(int percent);
        void onClose();
    }

    // Indices in MobileSettings::settings (libGTASA 2.10 setupValues order).
    static final int QUALITY = 0, RESOLUTION = 1, DRAW_DISTANCE = 2, SHADOWS = 5, CAR_REFLECTIONS = 7,
            SFX = 11, MUSIC = 12, BRIGHTNESS = 16, SUBTITLES = 24, VIBRATION = 29, FRAME_LIMITER = 30;
    private static final int[] SAVED = {QUALITY, RESOLUTION, DRAW_DISTANCE, SHADOWS, CAR_REFLECTIONS, SFX, MUSIC,
            BRIGHTNESS, SUBTITLES, VIBRATION, FRAME_LIMITER};
    private static final String PREFS = "neon_game_settings";
    static final String KEY_CHAT = "chat_shown";
    private static final String KEY_CHAT_ANCHOR = "chat_anchor";
    private static final String KEY_BUTTONS_ANCHOR = "hud_buttons_anchor";
    private static final String KEY_CHAT_SCALE = "chat_scale_percent";
    private static final String KEY_CHAT_TOUCH = "chat_touch";

    private final Host host;
    private final SharedPreferences prefs;
    private final LinearLayout list;
    private final TextView note;

    public GameSettingsView(Context context, Host host) {
        super(context);
        this.host = host;
        this.prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        setBackgroundColor(0xCC000000);
        setClickable(true);
        setOnClickListener(v -> host.onClose());

        LinearLayout card = new LinearLayout(context);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setClickable(true);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xF2141A22);
        bg.setCornerRadius(dp(18));
        bg.setStroke(dp(1), 0x33FFFFFF);
        card.setBackground(bg);
        card.setPadding(dp(20), dp(16), dp(20), dp(12));

        LinearLayout header = new LinearLayout(context);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = new TextView(context);
        title.setText("Settings");
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        header.addView(title, new LinearLayout.LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f));
        Button close = flatButton("Done", 0xFF2F6FD6, 0xFF2559AD);
        close.setOnClickListener(v -> host.onClose());
        header.addView(close, new LinearLayout.LayoutParams(dp(96), dp(40)));
        card.addView(header);

        note = new TextView(context);
        note.setTextColor(0xFFB7C3CE);
        note.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        note.setPadding(0, dp(6), 0, dp(4));
        card.addView(note);

        ScrollView scroll = new ScrollView(context);
        scroll.setVerticalScrollBarEnabled(true);
        list = new LinearLayout(context);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list);
        card.addView(scroll, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f));

        int width = Math.min(dp(620), getResources().getDisplayMetrics().widthPixels - dp(32));
        int height = getResources().getDisplayMetrics().heightPixels - dp(32);
        addView(card, new LayoutParams(width, height, Gravity.CENTER));
    }

    /** Rebuilds the rows from the live values. Call each time the screen opens. */
    public void refresh() {
        list.removeAllViews();
        boolean live = host.get(QUALITY) != null;
        note.setText(live ? "Changes apply at once and are kept for the next sessions."
                : "The game is still loading; the options appear once it is running.");

        section(getContext().getString(R.string.section_interface));
        interfaceScale();
        hudSettings();

        section("Graphics");
        choice("Visual quality", QUALITY, new String[] {"Low", "Medium", "High", "Very high"});
        slider("Resolution", RESOLUTION, "%");
        slider("Draw distance", DRAW_DISTANCE, "%");
        choice("Shadows", SHADOWS, new String[] {"Off", "Low", "High"});
        choice("Car reflections", CAR_REFLECTIONS, new String[] {"Off", "Low", "Medium", "High"});
        slider("Brightness", BRIGHTNESS, "%");
        choice("Frame limiter", FRAME_LIMITER, new String[] {"Off", "On"});

        section("Audio");
        slider("Sound effects", SFX, "%");
        slider("Game music", MUSIC, "%");

        section("Game");
        choice("Vibration", VIBRATION, new String[] {"Off", "On"});
        choice("Subtitles", SUBTITLES, new String[] {"Off", "On"});
        boolean chat = prefs.getBoolean(KEY_CHAT, true);
        row("Chat", segmented(new String[] {"Hidden", "Shown"}, chat ? 1 : 0, picked -> {
            prefs.edit().putBoolean(KEY_CHAT, picked == 1).apply();
            host.setChatShown(picked == 1);
        }));
    }

    /** Writes every saved choice into the game. Called when the player joins. */
    public static void applySaved(Context context, Host host) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        for (int index : SAVED) {
            String key = "gta_" + index;
            if (prefs.contains(key)) host.set(index, prefs.getInt(key, 0));
        }
        host.setChatShown(prefs.getBoolean(KEY_CHAT, true));
        applyHudSettings(context, host);
    }

    static void applyHudSettings(Context context, Host host) {
        SharedPreferences saved = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        host.setHudLayout(
                HudLayout.clamp(saved.getInt(KEY_CHAT_ANCHOR, HudLayout.TOP_LEFT), HudLayout.TOP_LEFT, HudLayout.BOTTOM_RIGHT),
                HudLayout.clamp(saved.getInt(KEY_BUTTONS_ANCHOR, HudLayout.FOLLOW_CHAT), HudLayout.TOP_LEFT, HudLayout.FOLLOW_CHAT),
                HudLayout.clamp(saved.getInt(KEY_CHAT_SCALE, HudLayout.DEFAULT_CHAT_PERCENT),
                        HudLayout.MIN_CHAT_PERCENT, HudLayout.MAX_CHAT_PERCENT),
                saved.getBoolean(KEY_CHAT_TOUCH, true));
    }

    private void hudSettings() {
        anchorChoice(R.string.hud_chat_anchor, KEY_CHAT_ANCHOR, HudLayout.TOP_LEFT, R.array.hud_chat_anchors);
        anchorChoice(R.string.hud_buttons_anchor, KEY_BUTTONS_ANCHOR, HudLayout.FOLLOW_CHAT, R.array.hud_button_anchors);

        LinearLayout box = new LinearLayout(getContext());
        box.setGravity(Gravity.CENTER_VERTICAL);
        SeekBar bar = new SeekBar(getContext());
        TextView value = new TextView(getContext());
        value.setTextColor(0xFFD5DEE8);
        int percent = HudLayout.clamp(prefs.getInt(KEY_CHAT_SCALE, HudLayout.DEFAULT_CHAT_PERCENT),
                HudLayout.MIN_CHAT_PERCENT, HudLayout.MAX_CHAT_PERCENT);
        value.setText(percent + "%");
        bar.setMax(HudLayout.MAX_CHAT_PERCENT - HudLayout.MIN_CHAT_PERCENT);
        bar.setProgress(percent - HudLayout.MIN_CHAT_PERCENT);
        bar.setContentDescription(getContext().getString(R.string.hud_chat_size));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int next = HudLayout.MIN_CHAT_PERCENT + progress;
                value.setText(next + "%");
                if (fromUser) {
                    prefs.edit().putInt(KEY_CHAT_SCALE, next).apply();
                    applyHudSettings(getContext(), host);
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        box.addView(bar, new LinearLayout.LayoutParams(0, dp(48), 1f));
        box.addView(value, new LinearLayout.LayoutParams(dp(56), LayoutParams.WRAP_CONTENT));
        row(getContext().getString(R.string.hud_chat_size), box);

        row(getContext().getString(R.string.hud_chat_touch),
                segmented(new String[]{getContext().getString(R.string.hud_off), getContext().getString(R.string.hud_on)},
                        prefs.getBoolean(KEY_CHAT_TOUCH, true) ? 1 : 0, picked -> {
                            prefs.edit().putBoolean(KEY_CHAT_TOUCH, picked == 1).apply();
                            applyHudSettings(getContext(), host);
                        }));
    }

    private void anchorChoice(int label, String key, int defaultValue, int names) {
        Spinner spinner = new Spinner(getContext(), Spinner.MODE_DROPDOWN);
        ArrayAdapter<CharSequence> options = ArrayAdapter.createFromResource(getContext(), names,
                android.R.layout.simple_spinner_item);
        options.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(options);
        spinner.setSelection(HudLayout.clamp(prefs.getInt(key, defaultValue), 0, options.getCount() - 1));
        spinner.setContentDescription(getContext().getString(label));
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (view instanceof TextView) ((TextView) view).setTextColor(Color.WHITE);
                if (prefs.getInt(key, defaultValue) == position) return;
                prefs.edit().putInt(key, position).apply();
                applyHudSettings(getContext(), host);
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
        row(getContext().getString(label), spinner);
    }

    private void store(int index, int value) {
        if (host.set(index, value)) prefs.edit().putInt("gta_" + index, value).apply();
    }

    private void interfaceScale() {
        TextView description = new TextView(getContext());
        description.setText(R.string.interface_scale_description);
        description.setTextColor(0xFFB7C3CE);
        description.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        list.addView(description);

        LinearLayout box = new LinearLayout(getContext());
        box.setGravity(Gravity.CENTER_VERTICAL);
        SeekBar bar = new SeekBar(getContext());
        bar.setContentDescription(getContext().getString(R.string.interface_scale));
        bar.setMax((InterfaceScale.MAX_PERCENT - InterfaceScale.MIN_PERCENT) / InterfaceScale.STEP_PERCENT);
        bar.setProgress((host.getInterfaceScalePercent() - InterfaceScale.MIN_PERCENT) / InterfaceScale.STEP_PERCENT);
        TextView value = new TextView(getContext());
        value.setTextColor(Color.WHITE);
        value.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        value.setText(host.getInterfaceScalePercent() + "%");
        Runnable apply = () -> {
            int percent = InterfaceScale.MIN_PERCENT + bar.getProgress() * InterfaceScale.STEP_PERCENT;
            if (!host.setInterfaceScalePercent(percent)) {
                Toast.makeText(getContext(), R.string.interface_scale_save_failed, Toast.LENGTH_SHORT).show();
                bar.setProgress((host.getInterfaceScalePercent() - InterfaceScale.MIN_PERCENT) / InterfaceScale.STEP_PERCENT);
            }
        };
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                value.setText((InterfaceScale.MIN_PERCENT + progress * InterfaceScale.STEP_PERCENT) + "%");
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) { apply.run(); }
        });
        box.addView(bar, new LinearLayout.LayoutParams(0, dp(48), 1f));
        box.addView(value, new LinearLayout.LayoutParams(dp(56), LayoutParams.WRAP_CONTENT));
        Button reset = flatButton(getContext().getString(R.string.interface_scale_default), 0xFF243140, 0xFF31435A);
        reset.setOnClickListener(v -> {
            bar.setProgress((InterfaceScale.DEFAULT_PERCENT - InterfaceScale.MIN_PERCENT) / InterfaceScale.STEP_PERCENT);
            apply.run();
        });
        box.addView(reset, new LinearLayout.LayoutParams(dp(80), dp(48)));
        row(getContext().getString(R.string.interface_scale), box);
    }

    private void section(String label) {
        TextView text = new TextView(getContext());
        text.setText(label.toUpperCase(java.util.Locale.ROOT));
        text.setTextColor(0xFF7FB8FF);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        text.setTypeface(Typeface.DEFAULT_BOLD);
        text.setLetterSpacing(0.08f);
        text.setPadding(0, dp(16), 0, dp(4));
        list.addView(text);
    }

    private void row(String label, View control) {
        LinearLayout row = new LinearLayout(getContext());
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(6), 0, dp(6));
        TextView text = new TextView(getContext());
        text.setText(label);
        text.setTextColor(Color.WHITE);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        row.addView(text, new LinearLayout.LayoutParams(dp(150), LayoutParams.WRAP_CONTENT));
        row.addView(control, new LinearLayout.LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f));
        list.addView(row);
    }

    private void choice(String label, int index, String[] names) {
        int[] live = host.get(index);
        if (live == null) return;
        int count = Math.min(names.length, live[2] - live[1] + 1);
        String[] shown = new String[count];
        System.arraycopy(names, 0, shown, 0, count);
        row(label, segmented(shown, live[0] - live[1], picked -> store(index, live[1] + picked)));
    }

    private void slider(String label, int index, String unit) {
        int[] live = host.get(index);
        if (live == null) return;
        LinearLayout box = new LinearLayout(getContext());
        box.setGravity(Gravity.CENTER_VERTICAL);
        SeekBar bar = new SeekBar(getContext());
        bar.setMax(live[2] - live[1]);
        bar.setProgress(live[0] - live[1]);
        TextView value = new TextView(getContext());
        value.setTextColor(0xFFD5DEE8);
        value.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        value.setGravity(Gravity.END);
        value.setText(live[0] + unit);
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int progress, boolean fromUser) {
                value.setText((live[1] + progress) + unit);
                if (fromUser) store(index, live[1] + progress);
            }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {}
        });
        box.addView(bar, new LinearLayout.LayoutParams(0, dp(40), 1f));
        box.addView(value, new LinearLayout.LayoutParams(dp(56), LayoutParams.WRAP_CONTENT));
        row(label, box);
    }

    private interface Picked { void on(int index); }

    private View segmented(String[] names, int selected, Picked picked) {
        LinearLayout group = new LinearLayout(getContext());
        Button[] buttons = new Button[names.length];
        for (int i = 0; i < names.length; i++) {
            final int at = i;
            Button button = flatButton(names[i], 0, 0);
            buttons[i] = button;
            button.setOnClickListener(v -> {
                for (int k = 0; k < buttons.length; k++) paintSegment(buttons[k], k == at);
                picked.on(at);
            });
            paintSegment(button, i == selected);
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, dp(38), 1f);
            if (i > 0) params.leftMargin = dp(4);
            group.addView(button, params);
        }
        return group;
    }

    private void paintSegment(Button button, boolean on) {
        button.setBackground(states(on ? 0xFF2F6FD6 : 0xFF243140, on ? 0xFF2559AD : 0xFF31435A));
        button.setTextColor(on ? Color.WHITE : 0xFFC9D3DE);
    }

    private Button flatButton(String label, int color, int pressed) {
        Button button = new Button(getContext());
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(Color.WHITE);
        button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        button.setPadding(dp(4), 0, dp(4), 0);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setMinHeight(0);
        button.setMinimumHeight(0);
        if (color != 0) button.setBackground(states(color, pressed));
        return button;
    }

    private StateListDrawable states(int color, int pressed) {
        GradientDrawable normal = new GradientDrawable();
        normal.setColor(color);
        normal.setCornerRadius(dp(10));
        GradientDrawable down = new GradientDrawable();
        down.setColor(pressed);
        down.setCornerRadius(dp(10));
        StateListDrawable list = new StateListDrawable();
        list.addState(new int[] {android.R.attr.state_pressed}, down);
        list.addState(new int[] {}, normal);
        return list;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
