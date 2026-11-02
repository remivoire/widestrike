/*
 * WideStrike - wstrike.ini parser (flat key=value, ';' or '#' comments)
 */
#include "wstrike.h"

ws_config g_cfg = {
    1,      /* enabled        */
    1,      /* log            */
    1,      /* fov = hor+     */
    1,      /* modes          */
    0,      /* max_width      */
    0,      /* max_height     */
    1,      /* clip_guard     */
    1,      /* force_software */
    1,      /* test_16bpp     */
    1,      /* cd_check       */
    1,      /* windowed       */
    1,      /* force_desktop  */
    1,      /* aspect         */
    0,      /* render_w       */
    0,      /* render_h       */
    ""      /* custom_modes   */
};

static char *ws_trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static int ws_atoi(const char *s)
{
    if (!s) return 0;
    if (!_stricmp(s, "true") || !_stricmp(s, "yes") || !_stricmp(s, "on")) return 1;
    if (!_stricmp(s, "false") || !_stricmp(s, "no") || !_stricmp(s, "off")) return 0;
    return atoi(s);
}

void ws_config_load(const char *dir)
{
    char path[MAX_PATH];
    char line[1024];
    FILE *fp;
    size_t n = 0;

    if (dir && dir[0]) {
        for (; dir[n] && n + 1 < sizeof(path); n++) path[n] = dir[n];
        if (n && path[n - 1] != '\\' && path[n - 1] != '/' && n + 1 < sizeof(path)) path[n++] = '\\';
    }
    path[n] = 0;
    strncat(path, "wstrike.ini", sizeof(path) - strlen(path) - 1);

    fp = fopen(path, "r");
    if (!fp) {
        /* first run: spawn a default config next to the exe */
        fp = fopen(path, "w");
        if (fp) {
            fputs("[wstrike]\n"
                  "; WideStrike - https://github.com/remivoire/widestrike\n"
                  "; Delete a line to use its default.\n"
                  ";\n"
                  ";   fov            1 = widescreen, 2 = crop vertically, 0 = original 4:3\n"
                  ";   aspect         1 = fill screen, 2 = 4:3 with side bars, 3 = integer scale\n"
                  ";   modes          1 = add every resolution the display offers\n"
                  ";   max_width      0 = desktop width  (cap for offered modes)\n"
                  ";   max_height     0 = desktop height\n"
                  ";   custom_modes   extra \"WxH,WxH\" entries always offered\n"
                  ";   windowed       1 = borderless window\n"
                  ";   cd_check       1 = skip the CD volume-label probe\n"
                  ";   clip_guard     1 = fix the projected-coordinate guards\n"
                  ";   force_software 1 = never use the 3dfx renderer\n"
                  ";   test_16bpp     1 = only offer modes the driver accepts at 16 bpp\n"
                  ";   enabled        0 = load and do nothing\n"
                  ";   log            1 = write wstrike.log\n"
                  ";   force_desktop / render_w / render_h : advanced, leave at 0\n"
                  "enabled=1\n"
                  "log=1\n"
                  "fov=1\n"
                  "aspect=1\n"
                  "modes=1\n"
                  "max_width=0\n"
                  "max_height=0\n"
                  "clip_guard=1\n"
                  "force_software=1\n"
                  "test_16bpp=1\n"
                  "cd_check=1\n"
                  "windowed=1\n"
                  "force_desktop=0\n"
                  "render_w=0\n"
                  "render_h=0\n"
                  "custom_modes=1280x720,1366x768,1600x900,1920x1080,2560x1440\n", fp);
            fclose(fp);
        }
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *s = ws_trim(line);
        char *eq, *key, *val;
        if (!s[0] || s[0] == ';' || s[0] == '#' || s[0] == '[') continue;
        eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        key = ws_trim(s);
        val = ws_trim(eq + 1);

        if      (!_stricmp(key, "enabled"))         g_cfg.enabled = ws_atoi(val);
        else if (!_stricmp(key, "log"))             g_cfg.log = ws_atoi(val);
        else if (!_stricmp(key, "fov"))             g_cfg.fov = ws_atoi(val);
        else if (!_stricmp(key, "modes"))           g_cfg.modes = ws_atoi(val);
        else if (!_stricmp(key, "max_width"))       g_cfg.max_width = ws_atoi(val);
        else if (!_stricmp(key, "max_height"))      g_cfg.max_height = ws_atoi(val);
        else if (!_stricmp(key, "clip_guard"))      g_cfg.clip_guard = ws_atoi(val);
        else if (!_stricmp(key, "force_software"))  g_cfg.force_software = ws_atoi(val);
        else if (!_stricmp(key, "test_16bpp"))      g_cfg.test_16bpp = ws_atoi(val);
        else if (!_stricmp(key, "cd_check"))        g_cfg.cd_check = ws_atoi(val);
        else if (!_stricmp(key, "windowed"))        g_cfg.windowed = ws_atoi(val);
        else if (!_stricmp(key, "force_desktop"))   g_cfg.force_desktop = ws_atoi(val);
        else if (!_stricmp(key, "aspect"))          g_cfg.aspect = ws_atoi(val);
        else if (!_stricmp(key, "render_w"))        g_cfg.render_w = ws_atoi(val);
        else if (!_stricmp(key, "render_h"))        g_cfg.render_h = ws_atoi(val);
        else if (!_stricmp(key, "custom_modes")) {
            strncpy(g_cfg.custom_modes, val, sizeof(g_cfg.custom_modes) - 1);
            g_cfg.custom_modes[sizeof(g_cfg.custom_modes) - 1] = 0;
        }
    }
    fclose(fp);

    if (g_cfg.fov < 0 || g_cfg.fov > 2) g_cfg.fov = 1;
}
