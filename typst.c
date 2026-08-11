/* ============================================================================
 *
 * typst clone
 * Typst-like C Rendering Engine 
 * Compile: gcc -Os -s -o typst.exe typst.c -lm -lgdi32 -luser32 -lgdi32
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_NODES        8000
#define MAX_STR_LEN      16384
#define MAX_TABLE_COLS   16
#define MAX_GRID_ROWS    128
#define MAX_COLORS       128
#define MAX_RUNS         256
#define STREAM_BUF_SIZE  1048576 

#define DPI              72.0
#define MIN(a, b)        ((a) < (b) ? (a) : (b))
#define MAX(a, b)        ((a) > (b) ? (a) : (b))

typedef struct {
    int r, g, b;
    const char* name;
} Color;

typedef struct {
    double font_size;
    Color fill_color;
    Color stroke_color;
    const char* font_name;
    int is_bold;
    int is_italic;
} TextState;

typedef struct {
    char text[MAX_STR_LEN];
    double font_size;
    int is_bold;
    int is_italic;
    Color color;
} TextRun;

typedef enum {
    NODE_TEXT, NODE_HEADING, NODE_PARAGRAPH, NODE_LIST, NODE_LIST_ITEM,
    NODE_GRID, NODE_TABLE, NODE_RECT, NODE_BLOCK, NODE_LINE, 
    NODE_VSPACE, NODE_HSPACE, NODE_PAGE, NODE_HEADER,
    NODE_PAGEBREAK, NODE_TSCORE_CHART, NODE_BMD_CHART, NODE_PIE_CHART,
    NODE_LINE_CHART, NODE_BAR_CHART
} NodeType;


typedef struct Node Node;

struct Node {
    NodeType type;
    int heading_level;
    char content[MAX_STR_LEN];

    double x, y, width, height;
    int has_stroke, has_width, has_height;
    int align; // 0: left, 1: center, 2: right
    
    Color stroke_color, fill_color;
    double stroke_width;
    double font_size;
    int is_bold;
    int is_italic;
    double radius;

    int alt_rows;
    Color header_fill;
    Color stripe_fill_1;
    Color stripe_fill_2;

    Node* cells[MAX_GRID_ROWS][MAX_TABLE_COLS];
    int cell_rows, cell_cols;
    double col_widths[MAX_TABLE_COLS];
    int col_aligns[MAX_TABLE_COLS];
    int is_fr[MAX_TABLE_COLS];
    double gutter;
    double inset;

    // Chart Data Fields
    char chart_title[128];
    char chart_labels[8][32];
    double chart_scores[8];
    int chart_count;
    char chart_v1[32];
    char chart_v2[32];
    char chart_pct[32];

    char chart_color[32];
    char chart_x_label[64];
    char chart_y_label[64];
    int chart_trend_line;

    Node* children[512];
    int child_count;
};

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StreamBuffer;

typedef struct {
    char name[64];
    int is_func;
    char param_name[64];
    char body[MAX_STR_LEN];
} LetDef;

/* =========================================
   GDI DISPLAY LIST STATE
   ========================================= */

#ifdef _WIN32
typedef enum {
    DL_TEXT, DL_RECT, DL_LINE, DL_PIE 
} DLType;
typedef struct {
    DLType type;
    int page;
    double x, y, w, h;
    Color c, stroke_c;
    double stroke_w;
    char text[1024];
    double font_size;
    int is_bold;
    int is_italic;
    double start_angle; 
    double end_angle;
} DLItem;

static DLItem dl_items[32768];
static int dl_count = 0;
static int current_render_page = 0;
static int max_page_num = 0;
static int current_view_page = 0;
#endif

/* =========================================
   GLOBAL STATE
   ========================================= */

static Color colors[MAX_COLORS];
static int color_count = 0;
static TextState current_state;
static char footer_script[MAX_STR_LEN] = {0};

static double page_width = 612.0;   
static double page_height = 792.0;  
static double margin_top = 70.86;   
static double margin_bottom = 70.86;
static double margin_left = 56.69;  
static double margin_right = 56.69; 

static Node nodes[MAX_NODES];
static int node_count = 0;
static char source[MAX_STR_LEN * 20];
static int source_pos = 0;

static char header_script[MAX_STR_LEN] = {0};

static LetDef let_defs[64];
static int let_def_count = 0;

/* =========================================
   FORWARD DECLARATIONS & PDF CONTEXT
   ========================================= */

#define MAX_PAGES 256

typedef struct {
    StreamBuffer pages[MAX_PAGES];
    int current_page;
int total_pages;
int disable_pagebreaks;
} PdfContext;

static Node* parse_element(void);
static Node* parse_typst_string(const char* input_str);
static void parse_let_directive(void);
static void render_node(PdfContext* ctx, Node* n, double* y, double max_w, double start_x);
static double measure_node_height(Node* n, double max_w);
void sanitize_utf8_to_winansi(char* dest, const char* src, size_t max_len);
static void parse_inline_runs(const char* in, TextState base_state, TextRun runs[], int* run_count);

/* =========================================
   IN-MEMORY STREAM BUFFER HELPERS
   ========================================= */

static void sb_init(StreamBuffer* sb) {
    sb->cap = STREAM_BUF_SIZE;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    if (sb->data) sb->data[0] = '\0';
}

static void sb_free(StreamBuffer* sb) {
    if (sb->data) free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_printf(StreamBuffer* sb, const char* fmt, ...) {
    if (!sb->data) return;
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed <= 0) return;
    while (sb->len + needed + 1 >= sb->cap) {
        sb->cap *= 2;
        char* new_data = (char*)realloc(sb->data, sb->cap);
        if (!new_data) return;
        sb->data = new_data;
    }
    va_start(args, fmt);
    vsnprintf(sb->data + sb->len, needed + 1, fmt, args);
    va_end(args);
    sb->len += needed;
}

/* =========================================
   COLOR MANAGEMENT
   ========================================= */

void add_color(const char* hex, const char* name) {
    if (color_count >= MAX_COLORS) return;
    Color* c = &colors[color_count++];
    if (hex[0] == '#') {
        sscanf(hex + 1, "%02x%02x%02x", &c->r, &c->g, &c->b);
    } else {
        c->r = c->g = c->b = 0;
    }
    c->name = name;
}

void init_colors(void) {
    add_color("#2B6CB0", "blue_primary");
    add_color("#2D3748", "gray_800");
    add_color("#4A5568", "gray_700");
    add_color("#718096", "gray_600");
    add_color("#A0AEC0", "gray_500");
    add_color("#CBD5E0", "gray_400");
    add_color("#E2E8F0", "gray_300");
    add_color("#EDF2F7", "gray_200");
    add_color("#F7FAFC", "gray_100");
    add_color("#FEFCBF", "yellow_light");
    add_color("#D69E2E", "yellow_border");
    add_color("#FFFFFF", "white");
    add_color("#000000", "black");
}

Color get_color(const char* spec) {
    Color fallback = {45, 55, 72, "gray_800"};
    if (!spec) return fallback;
    while (*spec == ' ' || *spec == '\t' || *spec == '+') spec++;
    if (!*spec) return fallback;

    if (strcmp(spec, "none") == 0 || strcmp(spec, "transparent") == 0) {
        return (Color){255, 255, 255, "none"};
    }
    if (strncmp(spec, "luma(", 5) == 0) {
        int val = 200;
        sscanf(spec + 5, "%d", &val);
        return (Color){val, val, val, "luma"};
    }
    if (strncmp(spec, "rgb(", 4) == 0) {
        const char* p = spec + 4;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '"' || *p == '\'') p++;
        if (*p == '#') {
            Color c = {0, 0, 0, "rgb_hex"};
            sscanf(p + 1, "%02x%02x%02x", &c.r, &c.g, &c.b);
            return c;
        }
        int r = 0, g = 0, b = 0;
        if (sscanf(p, "%d,%d,%d", &r, &g, &b) >= 3 || sscanf(p, "%d, %d, %d", &r, &g, &b) >= 3) {
            Color c = {r, g, b, "rgb_num"};
            return c;
        }
    }
    if (strcmp(spec, "white") == 0) return (Color){255, 255, 255, "white"};
    if (strcmp(spec, "black") == 0) return (Color){0, 0, 0, "black"};
    if (spec[0] == '#') {
        Color c = {0, 0, 0, "hex"};
        sscanf(spec + 1, "%02x%02x%02x", &c.r, &c.g, &c.b);
        return c;
    }
    for (int i = 0; i < color_count; i++) {
        if (colors[i].name && strcmp(colors[i].name, spec) == 0) return colors[i];
    }
    return fallback;
}

/* =========================================
   NODE ALLOCATION
   ========================================= */

Node* alloc_node(NodeType type) {
    if (node_count >= MAX_NODES) return NULL;
    Node* n = &nodes[node_count++];
    memset(n, 0, sizeof(Node));
    n->type = type;
    n->font_size = current_state.font_size > 0 ? current_state.font_size : 10.0;
    n->fill_color = current_state.fill_color;
    n->stripe_fill_1 = (Color){255, 255, 255, "white"};
    n->stripe_fill_2 = (Color){247, 250, 252, "gray_100"};
    n->header_fill = (Color){43, 108, 176, "blue_primary"};
    n->gutter = 0.0;
    n->inset = 0.0;
    return n;
}

/* =========================================
   LEXING & STRING HELPERS
   ========================================= */

static int skip_whitespace_and_comments(void) {
    while (source[source_pos]) {
        if (isspace((unsigned char)source[source_pos])) { source_pos++; continue; }
        if (source[source_pos] == '/' && source[source_pos+1] == '/') {
            while (source[source_pos] && source[source_pos] != '\n') source_pos++;
            continue;
        }
        if (source[source_pos] == '/' && source[source_pos+1] == '*') {
            source_pos += 2;
            while (source[source_pos]) {
                if (source[source_pos] == '*' && source[source_pos+1] == '/') { source_pos += 2; break; }
                source_pos++;
            }
            continue;
        }
        return source[source_pos];
    }
    return 0;
}

static void consume(void) { source_pos++; }

static int match_str(const char* s) {
    int pos = source_pos; 
    skip_whitespace_and_comments();
    if (strncmp(&source[source_pos], s, strlen(s)) == 0) {
        source_pos += strlen(s); return 1;
    }
    source_pos = pos;
    return 0;
}

static double parse_size(const char* s) {
    if (!s) return 0;
    double val = atof(s);
    if (strstr(s, "pt")) return val;
    if (strstr(s, "em")) return val * current_state.font_size;
    if (strstr(s, "cm")) return val * DPI / 2.54;
    if (strstr(s, "mm")) return val * DPI / 25.4;
    if (strstr(s, "in")) return val * DPI;
    return val;
}

static int extract_bracket_content(char* dest, int max_len) {
    int depth = 0, i = 0;
    skip_whitespace_and_comments();
    if (source[source_pos] != '[') return 0;
    depth = 1; source_pos++;
    while (source[source_pos] && depth > 0) {
        if (source[source_pos] == '[') depth++;
        else if (source[source_pos] == ']') depth--;
        if (depth > 0 && i < max_len - 1) dest[i++] = source[source_pos];
        source_pos++;
    }
    dest[i] = '\0';
    return 1;
}

static void unwrap_content_markup(char* script) {
    int changed = 1;
    while (changed) {
        changed = 0;
        char* p = script;
        while (*p && isspace((unsigned char)*p)) p++;
        
        // 1. Strip enclosing [...] brackets
        if (*p == '[') {
            p++;
            size_t len = strlen(p);
            while (len > 0 && isspace((unsigned char)p[len - 1])) len--;
            if (len > 0 && p[len - 1] == ']') {
                p[len - 1] = '\0';
                memmove(script, p, len);
                changed = 1;
                continue;
            }
        }
        
        // 1b. Strip enclosing {...} braces
        if (*p == '{') {
            p++;
            size_t len = strlen(p);
            while (len > 0 && isspace((unsigned char)p[len - 1])) len--;
            if (len > 0 && p[len - 1] == '}') {
                p[len - 1] = '\0';
                memmove(script, p, len);
                changed = 1;
                continue;
            }
        }
        
        // 2. Strip leading "context" keyword
        if (strncmp(p, "context", 7) == 0 && (isspace((unsigned char)p[7]) || p[7] == '\0' || p[7] == '{' || p[7] == '[')) {
            p += 7;
            while (*p && isspace((unsigned char)*p)) p++;
            memmove(script, p, strlen(p) + 1);
            changed = 1;
            continue;
        }
        
        // 3. Strip #text(...) or #align(...) function calls
        char* start = p;
        if (*start == '#') start++;
        if (strncmp(start, "text", 4) == 0 || strncmp(start, "align", 5) == 0) {
            char* q = start + (strncmp(start, "text", 4) == 0 ? 4 : 5);
            while (*q && isspace((unsigned char)*q)) q++;
            if (*q == '(') {
                int depth = 1;
                q++;
                while (*q && depth > 0) {
                    if (*q == '(') depth++;
                    else if (*q == ')') depth--;
                    q++;
                }
                while (*q && isspace((unsigned char)*q)) q++;
                memmove(script, q, strlen(q) + 1);
                changed = 1;
                continue;
            }
        }
    }
    
    // 4. Strip all remaining leading/trailing whitespace and newlines
    char* p = script;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len - 1])) len--;
    p[len] = '\0';
    if (p != script) {
        memmove(script, p, len + 1);
    }
}

static void unwrap_logic_statements(char* script) {
    int changed = 1;
    while (changed) {
        changed = 0;
        char* p = script;
        while (*p && isspace((unsigned char)*p)) p++;
        
        if (strncmp(p, "let ", 4) == 0) {
            char* eol = strchr(p, '\n');
            if (eol) p = eol + 1;
            else p += strlen(p);
            while (*p && isspace((unsigned char)*p)) p++;
            memmove(script, p, strlen(p) + 1);
            changed = 1;
            continue;
        }

        if (strncmp(p, "if ", 3) == 0) {
            char* bracket = strchr(p, '[');
            if (bracket) p = bracket; // keep the '[' so unwrap_content_markup can strip it on the next pass
            else p += strlen(p);
            memmove(script, p, strlen(p) + 1);
            changed = 1;
            continue;
        }
    }
}

static int extract_param_value(const char* params, const char* key, char* out_val, size_t max_len) {
    const char* p = params;
    size_t key_len = strlen(key);

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (strncmp(p, key, key_len) == 0) {
            const char* after_key = p + key_len;
            while (*after_key && isspace((unsigned char)*after_key)) after_key++;
            if (*after_key == ':') {
                p = after_key + 1;
                while (*p && isspace((unsigned char)*p)) p++;
                
                size_t idx = 0;
                int paren_depth = 0, bracket_depth = 0, brace_depth = 0;
                int started_with_bracket = (*p == '[');
                int started_with_brace = (*p == '{');
                
                while (*p) {
                    if (*p == '(') paren_depth++;
                    else if (*p == ')') paren_depth--;
                    else if (*p == '[') bracket_depth++;
                    else if (*p == ']') bracket_depth--;
                    else if (*p == '{') brace_depth++;
                    else if (*p == '}') brace_depth--;
                    
                    if (idx < max_len - 1) out_val[idx++] = *p;
                    p++;
                    
                    // Stop immediately when a starting '[...]' or '{...}' content block closes
                    if (started_with_bracket && bracket_depth == 0 && paren_depth == 0 && brace_depth == 0) {
                        break;
                    }
                    if (started_with_brace && bracket_depth == 0 && paren_depth == 0 && brace_depth == 0) {
                        break;
                    }
                    // Otherwise stop at a top-level comma
                    if (!started_with_bracket && !started_with_brace && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 && *p == ',') {
                        break;
                    }
                }
                while (idx > 0 && isspace((unsigned char)out_val[idx - 1])) idx--;
                out_val[idx] = '\0';
                return 1;
            }
        }
        
        // Advance past the next argument at depth 0
        int p_depth = 0, b_depth = 0, br_depth = 0;
        while (*p && (p_depth > 0 || b_depth > 0 || br_depth > 0 || *p != ',')) {
            if (*p == '(') p_depth++;
            else if (*p == ')') p_depth--;
            else if (*p == '[') b_depth++;
            else if (*p == ']') b_depth--;
            else if (*p == '{') br_depth++;
            else if (*p == '}') br_depth--;
            p++;
        }
        if (*p == ',') p++;
    }
    return 0;
}

/* =========================================
   TEXT RUN & INLINE STYLING PARSER
   ========================================= */

static void parse_inline_runs(const char* in, TextState base_state, TextRun runs[], int* run_count) {
    *run_count = 0;
    char sanitized_in[MAX_STR_LEN];
    sanitize_utf8_to_winansi(sanitized_in, in, sizeof(sanitized_in));

    TextState state = base_state;
    char buf[MAX_STR_LEN] = {0};
    int bi = 0;
    const char* p = sanitized_in;
    
    while (*p) {
        // --- 3-Byte UTF-8 Characters ---
        if ((unsigned char)*p == 0xE2) {
            if ((unsigned char)*(p+1) == 0x80) {
                if ((unsigned char)*(p+2) == 0xA2) { // Bullet (•)
                    buf[bi++] = '\x95';
                    p += 3; continue;
                } else if ((unsigned char)*(p+2) == 0x93) { // En-dash (–)
                    buf[bi++] = 150; 
                    p += 3; continue;
                } else if ((unsigned char)*(p+2) == 0x94) { // Em-dash (—)
                    buf[bi++] = 151; 
                    p += 3; continue;
                }
            } else if ((unsigned char)*(p+1) == 0x84 && (unsigned char)*(p+2) == 0xA2) { // Trademark (™)
                buf[bi++] = 153; // Fixed from '\x99' to prevent signed char issues
                p += 3; continue;
            }
        }

        // --- 2-Byte UTF-8 Characters ---
        if ((unsigned char)*p == 0xC2) {
            if ((unsigned char)*(p+1) == 0xA9) { // Copyright (©)
                buf[bi++] = 169;
                p += 2; continue;
            } else if ((unsigned char)*(p+1) == 0xAE) { // Registered (®)
                buf[bi++] = 174;
                p += 2; continue;
            } else if ((unsigned char)*(p+1) == 0xB0) { // Degree (°)
                buf[bi++] = 176;
                p += 2; continue;
            } else if ((unsigned char)*(p+1) == 0xB2) { // Superscript 2 (²)
                buf[bi++] = 178;
                p += 2; continue;
            } else if ((unsigned char)*(p+1) == 0xB7) { // Middle dot (·) - prevents 'Â' bug
                buf[bi++] = 183;
                p += 2; continue;
            }
        }

        // --- Literal ASCII Auto-Conversions ---
        if (strncmp(p, "(R)", 3) == 0 || strncmp(p, "(r)", 3) == 0) {
            buf[bi++] = 174; // ®
            p += 3; continue;
        }
        if (strncmp(p, "(C)", 3) == 0 || strncmp(p, "(c)", 3) == 0) {
            buf[bi++] = 169; // ©
            p += 3; continue;
        }
        if (strncmp(p, "(TM)", 4) == 0 || strncmp(p, "(tm)", 4) == 0) {
            buf[bi++] = 153; // ™
            p += 4; continue;
        }

        if (*p == '\\' && (*(p+1) == ' ' || *(p+1) == '\n' || *(p+1) == '\0')) {
            if (bi > 0) {
                buf[bi] = '\0';
                runs[*run_count] = (TextRun){"", state.font_size, state.is_bold, state.is_italic, state.fill_color};
                memcpy(runs[*run_count].text, buf, bi + 1);
                (*run_count)++; bi = 0; buf[0] = '\0';
            }
            runs[*run_count] = (TextRun){"\n", state.font_size, state.is_bold, state.is_italic, state.fill_color};
            (*run_count)++;
            p += (*(p+1) == '\n') ? 2 : 1;
            continue;
        }

        if (*p == '\\' && (*(p+1) == '#' || *(p+1) == '$' || *(p+1) == '*' || *(p+1) == '_')) {
            buf[bi++] = *(p+1); p += 2; continue;
        }

        if (*p == '*') {
            if (bi > 0) {
                buf[bi] = '\0';
                runs[*run_count] = (TextRun){"", state.font_size, state.is_bold, state.is_italic, state.fill_color};
                memcpy(runs[*run_count].text, buf, bi + 1);
                (*run_count)++; bi = 0; buf[0] = '\0';
            }
            state.is_bold = !state.is_bold;
            p++; continue;
        }

        if (strncmp(p, "#text(", 6) == 0 || strncmp(p, "text(", 5) == 0) {
            if (bi > 0) {
                buf[bi] = '\0';
                runs[*run_count] = (TextRun){"", state.font_size, state.is_bold, state.is_italic, state.fill_color};
                memcpy(runs[*run_count].text, buf, bi + 1);
                (*run_count)++; bi = 0; buf[0] = '\0';
            }
            int is_hash = (p[0] == '#');
            p += is_hash ? 6 : 5;

            char params[MAX_STR_LEN]; int pi = 0, depth = 1;
            while (*p && depth > 0) {
                if (*p == '(') depth++; else if (*p == ')') depth--;
                if (depth > 0 && pi < MAX_STR_LEN - 1) params[pi++] = *p;
                p++;
            }
            params[pi] = '\0';

            TextState local_state = state;
            char val[128];
            if (extract_param_value(params, "fill", val, sizeof(val))) local_state.fill_color = get_color(val);
            
            if (extract_param_value(params, "size", val, sizeof(val))) {
                local_state.font_size = parse_size(val);
            } else {
                char* pt_str = strstr(params, "pt");
                if (!pt_str) pt_str = strstr(params, "em");
                if (pt_str) {
                    char* start = pt_str - 1;
                    while (start >= params && (isdigit((unsigned char)*start) || *start == '.')) start--;
                    start++;
                    if (start < pt_str) local_state.font_size = atof(start);
                }
            }

            if (strstr(params, "bold") || strstr(params, "\"bold\"")) local_state.is_bold = 1;

            while (*p == ' ' || *p == '\t') p++;
            if (*p == '[') {
                p++;
                char inner[MAX_STR_LEN]; int ii = 0; depth = 1;
                while (*p && depth > 0) {
                    if (*p == '[') depth++; else if (*p == ']') depth--;
                    if (depth > 0 && ii < MAX_STR_LEN - 1) inner[ii++] = *p;
                    p++;
                }
                inner[ii] = '\0';

                int sub_count = 0;
                TextRun* sub_runs = (TextRun*)malloc(sizeof(TextRun) * MAX_RUNS);
                if (sub_runs) {
                    parse_inline_runs(inner, local_state, sub_runs, &sub_count);
                    for (int k = 0; k < sub_count && *run_count < MAX_RUNS; k++) {
                        runs[(*run_count)++] = sub_runs[k];
                    }
                    free(sub_runs); 
                }
            }
            continue;
        }
        buf[bi++] = *p++;
    }

    if (bi > 0 && *run_count < MAX_RUNS) {
        buf[bi] = '\0';
        runs[*run_count] = (TextRun){"", state.font_size, state.is_bold, state.is_italic, state.fill_color};
        memcpy(runs[*run_count].text, buf, bi + 1);
        (*run_count)++;
    }
}

/* =========================================
   MACRO & TYPST STRING PARSER
   ========================================= */

static Node* parse_typst_string(const char* input_str) {
    if (!input_str || !*input_str) return NULL;
    char* temp_source = (char*)malloc(sizeof(source));
    if (!temp_source) return NULL;
    memcpy(temp_source, source, sizeof(source));
    int temp_pos = source_pos;

    strncpy(source, input_str, sizeof(source) - 1);
    source[sizeof(source) - 1] = '\0';
    source_pos = 0;

    Node* block = alloc_node(NODE_BLOCK);
    while (skip_whitespace_and_comments() != 0) {
        int prev = source_pos;
        Node* elem = parse_element();
        if (elem && block && block->child_count < 512) {
            block->children[block->child_count++] = elem;
        }
        if (source_pos == prev) source_pos++;
        if (source[source_pos] == '\n') source_pos++;
    }

    memcpy(source, temp_source, sizeof(source));
    source_pos = temp_pos;
    free(temp_source);

    return block;
}

static void parse_let_directive(void) {
    match_str("#let");
    skip_whitespace_and_comments();
    
    char name[64] = {0};
    int ni = 0;
    while (source[source_pos] && (isalnum((unsigned char)source[source_pos]) || source[source_pos] == '_' || source[source_pos] == '-')) {
        if (ni < 63) name[ni++] = source[source_pos];
        source_pos++;
    }
    name[ni] = '\0';
    if (ni == 0) return;

    skip_whitespace_and_comments();
    
    LetDef* def = NULL;
    for (int i = 0; i < let_def_count; i++) {
        if (strcmp(let_defs[i].name, name) == 0) {
            def = &let_defs[i];
            break;
        }
    }
    if (!def && let_def_count < 64) {
        def = &let_defs[let_def_count++];
    }
    if (!def) return;
    
    memset(def, 0, sizeof(LetDef));
    strncpy(def->name, name, sizeof(def->name) - 1);

    if (source[source_pos] == '(') {
        def->is_func = 1;
        source_pos++;
        int pi = 0;
        while (source[source_pos] && source[source_pos] != ')' && source[source_pos] != '=') {
            if (pi < 63) def->param_name[pi++] = source[source_pos];
            source_pos++;
        }
        def->param_name[pi] = '\0';
        if (source[source_pos] == ')') source_pos++;
    }

    skip_whitespace_and_comments();
    if (source[source_pos] == '=') source_pos++;
    skip_whitespace_and_comments();

    if (strncmp(&source[source_pos], "rgb(", 4) == 0) {
        char val[128];
        int vi = 0;
        while (source[source_pos] && source[source_pos] != '\n' && vi < 127) {
            val[vi++] = source[source_pos++];
        }
        val[vi] = '\0';
        Color c = get_color(val);
        c.name = strdup(def->name);
        if (color_count < MAX_COLORS) colors[color_count++] = c;
        return;
    }

    if (source[source_pos] == '[') {
        /* Handle content block [ ... ] */
        int depth = 1;
        source_pos++;
        int bi = 0;
        while (source[source_pos] && depth > 0) {
            if (source[source_pos] == '[') depth++;
            else if (source[source_pos] == ']') depth--;
            if (depth > 0 && bi < MAX_STR_LEN - 1) {
                def->body[bi++] = source[source_pos];
            }
            source_pos++;
        }
        def->body[bi] = '\0';
        return;
    } else if (source[source_pos] == '{') {
        /* Handle code block { ... } — consume entire block, don't store body */
        int depth = 1;
        source_pos++;
        while (source[source_pos] && depth > 0) {
            if (source[source_pos] == '{') depth++;
            else if (source[source_pos] == '}') depth--;
            if (depth > 0) source_pos++;
        }
        if (source[source_pos] == '}') source_pos++;
        /* Mark as function with empty body so callers skip it gracefully */
        def->is_func = 1;
        def->body[0] = '\0';
        return;
    } else {
        int bi = 0;
        while (source[source_pos] && source[source_pos] != '\n' && bi < MAX_STR_LEN - 1) {
            def->body[bi++] = source[source_pos++];
        }
        def->body[bi] = '\0';
        return;
    }
}

/* =========================================
   ELEMENT PARSERS
   ========================================= */

static void parse_set_directive(void) {
    if (match_str("#set page(")) {
        char params[MAX_STR_LEN]; int pi = 0, depth = 1;
        while (source[source_pos] && depth > 0) {
            if (source[source_pos] == '(') depth++; else if (source[source_pos] == ')') depth--;
            if (depth > 0) params[pi++] = source[source_pos];
            source_pos++;
        }
        params[pi] = '\0';

        char val[MAX_STR_LEN];
        if (extract_param_value(params, "paper", val, sizeof(val))) {
            if (strstr(val, "us-letter") || strstr(val, "letter")) {
                page_width = 612.0; page_height = 792.0;
            } else if (strstr(val, "a4")) {
                page_width = 595.28; page_height = 841.89;
            }
        }
        
        if (extract_param_value(params, "margin", val, sizeof(val))) {
            // Remove enclosing parentheses from margin value
            char margin_inner[MAX_STR_LEN];
            if (val[0] == '(') {
                const char* start = val + 1;
                char* end = strrchr(val, ')');
                if (end) {
                    size_t len = end - start;
                    strncpy(margin_inner, start, len);
                    margin_inner[len] = '\0';
                } else {
                    strcpy(margin_inner, val);
                }
            } else {
                strcpy(margin_inner, val);
            }
            
            char mval[64];
            if (extract_param_value(margin_inner, "x", mval, sizeof(mval))) {
                margin_left = parse_size(mval);
                margin_right = parse_size(mval);
            }
            if (extract_param_value(margin_inner, "left", mval, sizeof(mval))) {
                margin_left = parse_size(mval);
            }
            if (extract_param_value(margin_inner, "right", mval, sizeof(mval))) {
                margin_right = parse_size(mval);
            }
            if (extract_param_value(margin_inner, "top", mval, sizeof(mval))) {
                margin_top = parse_size(mval);
            }
            if (extract_param_value(margin_inner, "bottom", mval, sizeof(mval))) {
                margin_bottom = parse_size(mval);
            }
        }

        char header_raw[MAX_STR_LEN];
        if (extract_param_value(params, "header", header_raw, sizeof(header_raw))) {
            strncpy(header_script, header_raw, sizeof(header_script) - 1);
            header_script[sizeof(header_script) - 1] = '\0';

            unwrap_content_markup(header_script);
            unwrap_logic_statements(header_script);
            unwrap_content_markup(header_script);

            char* counter_pos;
            while ((counter_pos = strstr(header_script, "#counter(page).display()")) != NULL) {
                memmove(counter_pos + 2, counter_pos + 24, strlen(counter_pos + 24) + 1);
                counter_pos[0] = '%';
                counter_pos[1] = 'd';
            }
            while ((counter_pos = strstr(header_script, "counter(page).display()")) != NULL) {
                memmove(counter_pos + 2, counter_pos + 23, strlen(counter_pos + 23) + 1);
                counter_pos[0] = '%';
                counter_pos[1] = 'd';
            }
        }

        char footer_raw[MAX_STR_LEN];
        if (extract_param_value(params, "footer", footer_raw, sizeof(footer_raw))) {
            strncpy(footer_script, footer_raw, sizeof(footer_script) - 1);
            footer_script[sizeof(footer_script) - 1] = '\0';

            unwrap_content_markup(footer_script);
            unwrap_logic_statements(footer_script);
            unwrap_content_markup(footer_script);

            char* counter_pos;
            while ((counter_pos = strstr(footer_script, "#counter(page).display()")) != NULL) {
                memmove(counter_pos + 2, counter_pos + 24, strlen(counter_pos + 24) + 1);
                counter_pos[0] = '%';
                counter_pos[1] = 'd';
            }
            while ((counter_pos = strstr(footer_script, "counter(page).display()")) != NULL) {
                memmove(counter_pos + 2, counter_pos + 23, strlen(counter_pos + 23) + 1);
                counter_pos[0] = '%';
                counter_pos[1] = 'd';
            }
        }
        return;
    }
    if (match_str("#set text(")) {
        char params[MAX_STR_LEN]; int pi = 0, depth = 1;
        while (source[source_pos] && depth > 0) {
            if (source[source_pos] == '(') depth++; else if (source[source_pos] == ')') depth--;
            if (depth > 0) params[pi++] = source[source_pos];
            source_pos++;
        }
        params[pi] = '\0';
        char val[128];
        if (extract_param_value(params, "fill", val, sizeof(val))) current_state.fill_color = get_color(val);
        
        if (extract_param_value(params, "size", val, sizeof(val))) {
            current_state.font_size = parse_size(val);
        } else {
            char* pt_str = strstr(params, "pt");
            if (!pt_str) pt_str = strstr(params, "em");
            if (pt_str) {
                char* start = pt_str - 1;
                while (start >= params && (isdigit((unsigned char)*start) || *start == '.')) start--;
                start++;
                if (start < pt_str) current_state.font_size = atof(start);
            }
        }
        return;
    }
    while (source[source_pos] && source[source_pos] != '\n') source_pos++;
}

static Node* parse_heading(void) {
    Node* h = alloc_node(NODE_HEADING);
    if (!h) return NULL;
    int level = 0;
    while (source[source_pos] == '=') { level++; consume(); }
    h->heading_level = level; 
    skip_whitespace_and_comments();
    int i = 0;
    while (source[source_pos] && source[source_pos] != '\n' && i < MAX_STR_LEN - 1) {
        h->content[i++] = source[source_pos++];
    }
    while (i > 0 && isspace((unsigned char)h->content[i-1])) i--;
    h->content[i] = '\0';
    h->font_size = 18.0 - (level - 1) * 3.0;
    h->is_bold = 1;
    return h;
}

static Node* parse_line(void) {
    Node* l = alloc_node(NODE_LINE);
    if (!l) return NULL;
    if (strncmp(&source[source_pos], "#line", 5) == 0) match_str("#line"); else match_str("line");
    if (source[source_pos] == '(') {
        match_str("(");
        char param[MAX_STR_LEN]; int pi = 0, depth = 1;
        while (source[source_pos] && depth > 0) {
            if (source[source_pos] == '(') depth++; else if (source[source_pos] == ')') depth--;
            if (depth > 0) param[pi++] = source[source_pos];
            source_pos++;
        }
        param[pi] = '\0';
        char stroke[MAX_STR_LEN];
        if (extract_param_value(param, "stroke", stroke, sizeof(stroke))) {
            l->has_stroke = 1;
            char* plus = strchr(stroke, '+');
            if (plus) { l->stroke_width = atof(stroke); l->stroke_color = get_color(plus + 1); }
            else { l->stroke_width = 0.5; l->stroke_color = get_color(stroke); }
        }
        char length[32];
        if (extract_param_value(param, "length", length, sizeof(length))) {
            if (strstr(length, "%")) {
                l->width = atof(length) / 100.0;
                l->has_width = 2;
            } else {
                l->width = parse_size(length);
                l->has_width = 1;
            }
        }
    }
    return l;
}

static void draw_pdf_rounded_rect(PdfContext* ctx, double x, double y, double w, double h, double radius, Color fill_c, int has_fill, int has_stroke, Color stroke_c, double stroke_w) {
    if (!ctx || (ctx->current_page < 0) || (ctx->current_page >= MAX_PAGES)) return;
    StreamBuffer* sb = &ctx->pages[ctx->current_page];

    #ifdef _WIN32
    if (dl_count < 32768) {
        DLItem* it = &dl_items[dl_count++];
        it->type = DL_RECT;
        it->page = ctx->current_page;
        it->x = x;
        it->y = y - h;
        it->w = w;
        it->h = h;
        it->c = has_fill ? fill_c : (Color){255, 255, 255, "none"};
        it->stroke_c = has_stroke ? stroke_c : (Color){0, 0, 0, "none"};
        it->stroke_w = has_stroke ? stroke_w : 0;
    }
    #endif

    double r = radius;
    if (r > w / 2.0) r = w / 2.0;
    if (r > h / 2.0) r = h / 2.0;

    // Aggressive threshold: force sharp rectangle operators for r <= 0.5
    if (r <= 0.5) {
        if (has_fill && has_stroke) {
            sb_printf(sb, "%.3f %.3f %.3f rg\n", fill_c.r / 255.0, fill_c.g / 255.0, fill_c.b / 255.0);
            sb_printf(sb, "%.3f %.3f %.3f RG %.2f w\n", stroke_c.r / 255.0, stroke_c.g / 255.0, stroke_c.b / 255.0, stroke_w);
            sb_printf(sb, "%.2f %.2f %.2f %.2f re B\n0 0 0 RG\n", x, y - h, w, h);
        } else if (has_fill) {
            sb_printf(sb, "%.3f %.3f %.3f rg\n", fill_c.r / 255.0, fill_c.g / 255.0, fill_c.b / 255.0);
            sb_printf(sb, "%.2f %.2f %.2f %.2f re f\n", x, y - h, w, h);
        } else if (has_stroke) {
            sb_printf(sb, "%.3f %.3f %.3f RG %.2f w\n", stroke_c.r / 255.0, stroke_c.g / 255.0, stroke_c.b / 255.0, stroke_w);
            sb_printf(sb, "%.2f %.2f %.2f %.2f re S\n0 0 0 RG\n", x, y - h, w, h);
        }
        return;
    }

    double k = 0.5522847498 * r;
    double x0 = x;
    double y0 = y - h;
    double x1 = x + w;
    double y1 = y;

    if (has_fill) {
        sb_printf(sb, "%.3f %.3f %.3f rg\n", fill_c.r / 255.0, fill_c.g / 255.0, fill_c.b / 255.0);
    }
    if (has_stroke) {
        sb_printf(sb, "%.3f %.3f %.3f RG %.2f w\n", stroke_c.r / 255.0, stroke_c.g / 255.0, stroke_c.b / 255.0, stroke_w);
    }

    // Construct single continuous path with cubic Bezier corner arcs
    sb_printf(sb, "%.2f %.2f m\n", x0 + r, y0);
    sb_printf(sb, "%.2f %.2f l\n", x1 - r, y0);
    sb_printf(sb, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x1 - r + k, y0, x1, y0 + r - k, x1, y0 + r);
    sb_printf(sb, "%.2f %.2f l\n", x1, y1 - r);
    sb_printf(sb, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x1, y1 - r + k, x1 - r + k, y1, x1 - r, y1);
    sb_printf(sb, "%.2f %.2f l\n", x0 + r, y1);
    sb_printf(sb, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x0 + r - k, y1, x0, y1 - r + k, x0, y1 - r);
    sb_printf(sb, "%.2f %.2f l\n", x0, y0 + r);
    sb_printf(sb, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x0, y0 + r - k, x0 + r - k, y0, x0 + r, y0);
    sb_printf(sb, "h ");

    if (has_fill && has_stroke) {
        sb_printf(sb, "B\n");
    } else if (has_fill) {
        sb_printf(sb, "f\n");
    } else if (has_stroke) {
        sb_printf(sb, "S\n");
    } else {
        sb_printf(sb, "n\n");
    }

    if (has_stroke) {
        sb_printf(sb, "0 0 0 RG\n");
    }
}

static Node* parse_block_node(NodeType type) {
    Node* b = alloc_node(type);
    if (!b) return NULL;

    // 1. Explicitly initialize defaults immediately after allocation
    b->radius = 0.0;
    b->fill_color = (Color){255, 255, 255, "none"};

    // 2. Match tag names (#rect, #box, #block, etc.)
    if (type == NODE_RECT) {
        if (strncmp(&source[source_pos], "#rect", 5) == 0) match_str("#rect");
        else if (strncmp(&source[source_pos], "#box", 4) == 0) match_str("#box");
        else if (strncmp(&source[source_pos], "box", 3) == 0) match_str("box");
        else match_str("rect");
    } else {
        if (strncmp(&source[source_pos], "#block", 6) == 0) match_str("#block");
        else match_str("block");
    }
    skip_whitespace_and_comments();
    
    // 3. Declare and extract parameter string from (...)
    char param[MAX_STR_LEN] = {0}; 
    int expects_closing_paren = 0;
    
    if (source[source_pos] == '(') {
        source_pos++;
        int pi = 0, depth = 1;
        while (source[source_pos] && depth > 0) {
            if (depth == 1 && source[source_pos] == '[') {
                expects_closing_paren = 0;
                break;
            }
            if (source[source_pos] == '(') depth++; else if (source[source_pos] == ')') depth--;
            if (depth > 0 && pi < MAX_STR_LEN - 1) param[pi++] = source[source_pos];
            source_pos++;
        }
        param[pi] = '\0';
    }

    // 4. Parse individual parameters safely
    char val[MAX_STR_LEN];

    val[0] = '\0';
    if (extract_param_value(param, "fill", val, sizeof(val)) && val[0] != '\0') {
        b->fill_color = get_color(val);
    }

    val[0] = '\0';
    if (extract_param_value(param, "stroke", val, sizeof(val)) && val[0] != '\0') {
        b->has_stroke = 1;
        char* plus = strchr(val, '+');
        if (plus) {
            b->stroke_width = atof(val);
            b->stroke_color = get_color(plus + 1);
        } else {
            b->stroke_width = 0.5;
            b->stroke_color = get_color(val);
        }
    }

    val[0] = '\0';
    if (extract_param_value(param, "inset", val, sizeof(val)) && val[0] != '\0') {
        b->inset = parse_size(val);
    }

    if (param[0] != '\0' && strstr(param, "radius") != NULL) {
        val[0] = '\0';
        if (extract_param_value(param, "radius", val, sizeof(val)) && val[0] != '\0') {
            b->radius = parse_size(val);
        }
    }

    val[0] = '\0';
    if (extract_param_value(param, "width", val, sizeof(val)) && val[0] != '\0') {
        if (strstr(val, "%")) {
            b->width = atof(val) / 100.0;
            b->has_width = 2; 
        } else {
            b->width = parse_size(val);
            b->has_width = 1; 
        }
    }

    // 5. Parse child content [...]
    skip_whitespace_and_comments();
    if (source[source_pos] == '[') {
        int start = source_pos + 1;
        int depth = 1; source_pos++;
        while (source[source_pos] && depth > 0) {
            if (source[source_pos] == '[') depth++; else if (source[source_pos] == ']') depth--;
            source_pos++;
        }
        char saved = source[source_pos - 1];
        source[source_pos - 1] = '\0';
        int saved_pos = source_pos;
        source_pos = start;
        
        while (skip_whitespace_and_comments() != 0) {
            int prev = source_pos;
            Node* elem = parse_element();
            if (elem && b->child_count < 512) b->children[b->child_count++] = elem;
            if (source_pos == prev) source_pos++;
        }
        source[saved_pos - 1] = saved;
        source_pos = saved_pos;
        
        if (expects_closing_paren) {
            skip_whitespace_and_comments();
            if (source[source_pos] == ')') source_pos++;
        }
    }
    return b;
}

static Node* parse_table_or_grid(NodeType type) {
    Node* t = alloc_node(type);
    if (!t) return NULL;
    if (type == NODE_GRID) {
        if (strncmp(&source[source_pos], "#grid", 5) == 0) match_str("#grid"); else match_str("grid");
    } else {
        if (strncmp(&source[source_pos], "#table", 6) == 0) match_str("#table"); else match_str("table");
    }
    skip_whitespace_and_comments();
    
    char param[MAX_STR_LEN] = {0};
    if (source[source_pos] == '(') {
        source_pos++;
        int pi = 0, depth = 1;
        while (source[source_pos] && depth > 0) {
            if (depth == 1 && (source[source_pos] == '[' || 
                              strncmp(&source[source_pos], "rect(", 5) == 0 ||
                              strncmp(&source[source_pos], "#rect(", 6) == 0 ||
                              strncmp(&source[source_pos], "box(", 4) == 0 ||
                              strncmp(&source[source_pos], "#box(", 5) == 0 ||
                              strncmp(&source[source_pos], "block(", 6) == 0 ||
                              strncmp(&source[source_pos], "#block(", 7) == 0 ||
                              strncmp(&source[source_pos], "align(", 6) == 0 ||
                              strncmp(&source[source_pos], "#align(", 7) == 0 ||
                              strncmp(&source[source_pos], "line(", 5) == 0 ||
                              strncmp(&source[source_pos], "#line(", 6) == 0 ||
                              strncmp(&source[source_pos], "table(", 6) == 0 ||
                              strncmp(&source[source_pos], "#table(", 7) == 0 ||
                              strncmp(&source[source_pos], "grid(", 5) == 0 ||
                              strncmp(&source[source_pos], "#grid(", 6) == 0)) {
                break;
            }
            if (source[source_pos] == '(') depth++; else if (source[source_pos] == ')') depth--;
            if (depth > 0 && pi < MAX_STR_LEN - 1) param[pi++] = source[source_pos];
            source_pos++;
        }
        param[pi] = '\0';
    }

    char val[MAX_STR_LEN];
    if (extract_param_value(param, "columns", val, sizeof(val))) {
        t->cell_cols = 0;
        char* p = val;
        while (*p) {
            while (*p && (isspace((unsigned char)*p) || *p == '(' || *p == ')' || *p == ',')) p++;
            if (!*p) break;
            if (strncmp(p, "1fr", 3) == 0 || strncmp(p, "fr", 2) == 0) {
                t->col_widths[t->cell_cols] = 1.0;
                t->is_fr[t->cell_cols] = 1;
                t->cell_cols++;
                while (*p && *p != ',') p++;
            } else if (isdigit((unsigned char)*p)) {
                double w = atof(p);
                while (*p && (isdigit((unsigned char)*p) || *p == '.')) p++;
                while (*p && isspace((unsigned char)*p)) p++;
                if (strncmp(p, "fr", 2) == 0) {
                    t->col_widths[t->cell_cols] = w;
                    t->is_fr[t->cell_cols] = 1;
                    while (*p && *p != ',' && *p != ')') p++;
                } else {
                    t->col_widths[t->cell_cols] = w;
                    t->is_fr[t->cell_cols] = 0;
                }
                t->cell_cols++;
            } else {
                p++;
            }
        }
    }

    if (extract_param_value(param, "align", val, sizeof(val))) {
        if (val[0] == '(') {
            int col_idx = 0;
            char* p = val + 1;
            while (*p && col_idx < MAX_TABLE_COLS) {
                while (*p == ' ' || *p == '\t' || *p == ',') p++;
                if (*p == ')' || !*p) break;
                if (strncmp(p, "center", 6) == 0) t->col_aligns[col_idx++] = 1;
                else if (strncmp(p, "right", 5) == 0) t->col_aligns[col_idx++] = 2;
                else if (strncmp(p, "left", 4) == 0) t->col_aligns[col_idx++] = 0;
                while (*p && *p != ',' && *p != ')') p++;
            }
        } else {
            int default_align = 0;
            if (strstr(val, "center")) default_align = 1;
            else if (strstr(val, "right")) default_align = 2;
            for (int i = 0; i < MAX_TABLE_COLS; i++) t->col_aligns[i] = default_align;
        }
    }

    char stroke_val[MAX_STR_LEN];
    if (extract_param_value(param, "stroke", stroke_val, sizeof(stroke_val))) {
        t->has_stroke = 1;
        char* plus = strchr(stroke_val, '+');
        if (plus) { 
            t->stroke_width = atof(stroke_val); 
            t->stroke_color = get_color(plus + 1); 
        } else { 
            t->stroke_width = 0.5; 
            t->stroke_color = get_color(stroke_val); 
        }
    }

    if (extract_param_value(param, "fill", val, sizeof(val))) {
        if (strstr(val, "calc.even") || strstr(val, "row") || strstr(val, "col-accent")) {
            t->alt_rows = 1;
            t->header_fill = get_color("col-accent");
            // Fixed zebra stripe colors so alternating rows have distinct backgrounds
            t->stripe_fill_1 = (Color){255, 255, 255, "white"};
            t->stripe_fill_2 = (Color){247, 250, 252, "gray_100"};
        } else {
            t->fill_color = get_color(val);
        }
    }

    if (extract_param_value(param, "inset", val, sizeof(val))) {
        t->inset = parse_size(val);
    }

    if (extract_param_value(param, "gutter", val, sizeof(val))) {
        t->gutter = parse_size(val);
    }

    int cell = 0;
    while (source[source_pos]) {
        skip_whitespace_and_comments();
        if (source[source_pos] == ')' || source[source_pos] == ']') { 
            if (source[source_pos] == ')') consume(); 
            break; 
        }

        int row = cell / t->cell_cols; 
        int col = cell % t->cell_cols;
        Node* cell_node = alloc_node(NODE_BLOCK);
        if (!cell_node) break;
        cell_node->align = t->col_aligns[col];

        if (strncmp(&source[source_pos], "align(", 6) == 0 || strncmp(&source[source_pos], "#align(", 7) == 0) {
            if (source[source_pos] == '#') match_str("#align("); else match_str("align(");
            if (match_str("right")) cell_node->align = 2;
            else if (match_str("center")) cell_node->align = 1;
            while (source[source_pos] && source[source_pos] != '[') source_pos++;
        }

        if (source[source_pos] == '[') {
            int start = source_pos + 1;
            int depth_b = 1; source_pos++;
            while (source[source_pos] && depth_b > 0) {
                if (source[source_pos] == '[') depth_b++; else if (source[source_pos] == ']') depth_b--;
                source_pos++;
            }
            char saved = source[source_pos - 1]; source[source_pos - 1] = '\0';
            int saved_pos = source_pos; source_pos = start;
            
            while (skip_whitespace_and_comments() != 0) {
                int prev = source_pos;
                Node* elem = parse_element();
                if (elem && cell_node->child_count < 512) cell_node->children[cell_node->child_count++] = elem;
                if (source_pos == prev) source_pos++;
            }
            source[saved_pos - 1] = saved; source_pos = saved_pos;
            for (int k = 0; k < cell_node->child_count; k++) cell_node->children[k]->align = cell_node->align;
        } else {
            Node* elem = parse_element();
            if (elem) {
                cell_node->children[cell_node->child_count++] = elem;
                elem->align = cell_node->align;
            }
        }

        if (row < MAX_GRID_ROWS && col < MAX_TABLE_COLS) t->cells[row][col] = cell_node;
        cell++;

        skip_whitespace_and_comments();
        if (source[source_pos] == ',') consume();
    }
    t->cell_rows = (cell + t->cell_cols - 1) / t->cell_cols;
    return t;
}

static Node* parse_plain_text(void) {
    Node* p = alloc_node(NODE_PARAGRAPH);
    if (!p) return NULL;
    int i = 0;
    while (source[source_pos] && source[source_pos] != '\n' && i < MAX_STR_LEN - 1) {
        p->content[i++] = source[source_pos++];
    }
    while (i > 0 && isspace((unsigned char)p->content[i-1])) i--;
    p->content[i] = '\0';
    if (i == 0) return NULL;
    return p;
}

static Node* parse_element(void) {
    skip_whitespace_and_comments();
    if (!source[source_pos]) return NULL;

    if (source[source_pos] == '=') return parse_heading();

    if (strncmp(&source[source_pos], "#set", 4) == 0) { parse_set_directive(); return NULL; }
    if (strncmp(&source[source_pos], "#let", 4) == 0) { parse_let_directive(); return NULL; }

    if (strncmp(&source[source_pos], "#pagebreak", 10) == 0) {
        match_str("#pagebreak");
        skip_whitespace_and_comments();
        if (source[source_pos] == '(') {
            source_pos++;
            skip_whitespace_and_comments();
            if (source[source_pos] == ')') source_pos++;
        }
        return alloc_node(NODE_PAGEBREAK);
    }

    if (strncmp(&source[source_pos], "#show", 5) == 0 ||
        strncmp(&source[source_pos], "#import", 7) == 0) {
        while (source[source_pos] && source[source_pos] != '\n') source_pos++;
        return NULL;
    }

    if (strncmp(&source[source_pos], "#v", 2) == 0 && (source[source_pos+2] == '(' || source[source_pos+2] == ' ')) {
        match_str("#v"); skip_whitespace_and_comments();
        if (source[source_pos] == '(') source_pos++;
        char val[32]; int i = 0;
        while (source[source_pos] && source[source_pos] != ')' && i < 31) {
            if (!isspace((unsigned char)source[source_pos])) val[i++] = source[source_pos];
            source_pos++;
        }
        if (source[source_pos] == ')') source_pos++;
        val[i] = '\0';
        Node* v = alloc_node(NODE_VSPACE);
        if (v) v->height = parse_size(val);
        return v;
    }

    if (source[source_pos] == '-' && isspace((unsigned char)source[source_pos+1])) {
        source_pos += 2;
        Node* p = alloc_node(NODE_PARAGRAPH);
        if (!p) return NULL;
        p->content[0] = '*';
        p->content[1] = '\x95';
        p->content[2] = '*';
        p->content[3] = ' ';
        int i = 4;
        while (source[source_pos] && source[source_pos] != '\n' && i < MAX_STR_LEN - 1) {
            p->content[i++] = source[source_pos++];
        }
        while (i > 0 && isspace((unsigned char)p->content[i-1])) i--;
        p->content[i] = '\0';
        return p;
    }

    if (source[source_pos] == '#') {
        int p = source_pos + 1;
        char name[64] = {0};
        int ni = 0;
        while (source[p] && (isalnum((unsigned char)source[p]) || source[p] == '_' || source[p] == '-')) {
            if (ni < 63) name[ni++] = source[p];
            p++;
        }
        name[ni] = '\0';

        if (strcmp(name, "report-header") == 0) {
            source_pos = p;
            skip_whitespace_and_comments();
            char subtitle[256] = "Comprehensive DXA Report";
            if (source[source_pos] == '(') {
                source_pos++;
                int si = 0;
                while (source[source_pos] && source[source_pos] != ')' && source[source_pos] != '\n') {
                    if (source[source_pos] != '"' && source[source_pos] != '\'') {
                        if (si < 255) subtitle[si++] = source[source_pos];
                    }
                    source_pos++;
                }
                subtitle[si] = '\0';
                if (source[source_pos] == ')') source_pos++;
            }
            for (int i = 0; i < let_def_count; i++) {
                if (strcmp(let_defs[i].name, "report-header") == 0) {
                    char expanded[MAX_STR_LEN];
                    strncpy(expanded, let_defs[i].body, sizeof(expanded) - 1);
                    expanded[sizeof(expanded) - 1] = '\0';
                    char* sub_ptr = strstr(expanded, "#subtitle");
                    if (sub_ptr) {
                        char tail[MAX_STR_LEN];
                        strncpy(tail, sub_ptr + 9, sizeof(tail) - 1);
                        *sub_ptr = '\0';
                        strncat(expanded, subtitle, sizeof(expanded) - strlen(expanded) - 1);
                        strncat(expanded, tail, sizeof(expanded) - strlen(expanded) - 1);
                    }
                    return parse_typst_string(expanded);
                }
            }
        }

        if (strcmp(name, "patient-info-block") == 0) {
            source_pos = p;
            for (int i = 0; i < let_def_count; i++) {
                if (strcmp(let_defs[i].name, "patient-info-block") == 0) {
                    return parse_typst_string(let_defs[i].body);
                }
            }
        }
        if (strcmp(name, "pie-chart") == 0) {
            source_pos = p;
            skip_whitespace_and_comments();
            char params[MAX_STR_LEN] = {0};
            if (source[source_pos] == '(') {
                source_pos++;
                int pi = 0, depth = 1;
                while (source[source_pos] && depth > 0) {
                    if (source[source_pos] == '(') depth++;
                    else if (source[source_pos] == ')') depth--;
                    if (depth > 0 && pi < MAX_STR_LEN - 1) params[pi++] = source[source_pos];
                    source_pos++;
                }
                params[pi] = '\0';
            }
            Node* chart = alloc_node(NODE_PIE_CHART);
            if (!chart) return NULL;
            
            chart->chart_title[0] = '\0';
            extract_param_value(params, "title", chart->chart_title, sizeof(chart->chart_title));
            unwrap_content_markup(chart->chart_title);
            
            const char* ptr = params;
            chart->chart_count = 0;
            while (*ptr && chart->chart_count < 8) {
                ptr = strstr(ptr, "(\"");
                if (!ptr) break;
                ptr += 2;
                int i = 0;
                while (*ptr && *ptr != '"' && i < 31) {
                    chart->chart_labels[chart->chart_count][i++] = *ptr++;
                }
                chart->chart_labels[chart->chart_count][i] = '\0';
                
                while (*ptr && *ptr != ',') ptr++; 
                if (*ptr == ',') ptr++;
                while (*ptr && isspace((unsigned char)*ptr)) ptr++;
                
                chart->chart_scores[chart->chart_count] = atof(ptr);
                chart->chart_count++;
            }
            return chart;
        }
if (strcmp(name, "line-chart") == 0) {
            source_pos = p;
            skip_whitespace_and_comments();
            char params[MAX_STR_LEN] = {0};
            if (source[source_pos] == '(') {
                source_pos++;
                int pi = 0, depth = 1;
                while (source[source_pos] && depth > 0) {
                    if (source[source_pos] == '(') depth++;
                    else if (source[source_pos] == ')') depth--;
                    if (depth > 0 && pi < MAX_STR_LEN - 1) params[pi++] = source[source_pos];
                    source_pos++;
                }
                params[pi] = '\0';
            }

            Node* chart = alloc_node(NODE_LINE_CHART);
            if (!chart) return NULL;
            
            chart->chart_title[0] = '\0';
            chart->chart_x_label[0] = '\0';
            chart->chart_y_label[0] = '\0';
            chart->chart_color[0] = '\0';
            chart->chart_trend_line = 0;
            
            // Simplified title parsing mirroring pie-chart
            extract_param_value(params, "title", chart->chart_title, sizeof(chart->chart_title));
            unwrap_content_markup(chart->chart_title);
            
            extract_param_value(params, "x-label", chart->chart_x_label, sizeof(chart->chart_x_label));
            unwrap_content_markup(chart->chart_x_label);

            extract_param_value(params, "y-label", chart->chart_y_label, sizeof(chart->chart_y_label));
            unwrap_content_markup(chart->chart_y_label);

            if (extract_param_value(params, "color", chart->chart_color, sizeof(chart->chart_color))) {
                unwrap_content_markup(chart->chart_color);
            } else if (strstr(params, "\"random\"") != NULL || strstr(params, "random") != NULL) {
                strcpy(chart->chart_color, "random");
            }
            if (chart->chart_color[0] == '\0') {
                strcpy(chart->chart_color, "#2563eb");
            }

            char tl_buf[32] = {0};
            if (extract_param_value(params, "trend-line", tl_buf, sizeof(tl_buf))) {
                if (strstr(tl_buf, "true")) chart->chart_trend_line = 1;
            }

            const char* ptr = params;
            chart->chart_count = 0;
            
            // Extracts labels identically to pie-chart, grabbing the string inside ("...")
            while (*ptr && chart->chart_count < 8) {
                ptr = strstr(ptr, "(\"");
                if (!ptr) break;
                ptr += 2;
                int i = 0;
                while (*ptr && *ptr != '"' && i < 31) {
                    chart->chart_labels[chart->chart_count][i++] = *ptr++;
                }
                chart->chart_labels[chart->chart_count][i] = '\0';
                
                while (*ptr && *ptr != ',') ptr++; 
                if (*ptr == ',') ptr++;
                while (*ptr && isspace((unsigned char)*ptr)) ptr++;
                
                chart->chart_scores[chart->chart_count] = atof(ptr);
                chart->chart_count++;
            }
            return chart;
        }
if (strcmp(name, "bar-chart") == 0) {
            source_pos = p;
            skip_whitespace_and_comments();
            char params[MAX_STR_LEN] = {0};
            if (source[source_pos] == '(') {
                source_pos++;
                int pi = 0, depth = 1;
                while (source[source_pos] && depth > 0) {
                    if (source[source_pos] == '(') depth++;
                    else if (source[source_pos] == ')') depth--;
                    if (depth > 0 && pi < MAX_STR_LEN - 1) params[pi++] = source[source_pos];
                    source_pos++;
                }
                params[pi] = '\0';
            }

            Node* chart = alloc_node(NODE_BAR_CHART);
            if (!chart) return NULL;
            
            chart->chart_title[0] = '\0';
            chart->chart_x_label[0] = '\0';
            chart->chart_y_label[0] = '\0';
            chart->chart_color[0] = '\0';
            
            // Simplified title parsing mirroring pie-chart
            extract_param_value(params, "title", chart->chart_title, sizeof(chart->chart_title));
            unwrap_content_markup(chart->chart_title);
            
            extract_param_value(params, "x-label", chart->chart_x_label, sizeof(chart->chart_x_label));
            unwrap_content_markup(chart->chart_x_label);

            extract_param_value(params, "y-label", chart->chart_y_label, sizeof(chart->chart_y_label));
            unwrap_content_markup(chart->chart_y_label);

            if (extract_param_value(params, "color", chart->chart_color, sizeof(chart->chart_color))) {
                unwrap_content_markup(chart->chart_color);
            } else if (strstr(params, "\"random\"") != NULL || strstr(params, "random") != NULL) {
                strcpy(chart->chart_color, "random");
            }

            const char* ptr = params;
            chart->chart_count = 0;
            
            // Extracts labels identically to pie-chart, grabbing the string inside ("...")
            while (*ptr && chart->chart_count < 8) {
                ptr = strstr(ptr, "(\"");
                if (!ptr) break;
                ptr += 2;
                int i = 0;
                while (*ptr && *ptr != '"' && i < 31) {
                    chart->chart_labels[chart->chart_count][i++] = *ptr++;
                }
                chart->chart_labels[chart->chart_count][i] = '\0';
                
                while (*ptr && *ptr != ',') ptr++; 
                if (*ptr == ',') ptr++;
                while (*ptr && isspace((unsigned char)*ptr)) ptr++;
                
                chart->chart_scores[chart->chart_count] = atof(ptr);
                chart->chart_count++;
            }
            return chart;
        }
        if (strcmp(name, "tscore-bar-chart") == 0) {
            source_pos = p;
            skip_whitespace_and_comments();
            char params[MAX_STR_LEN] = {0};
            if (source[source_pos] == '(') {
                source_pos++;
                int pi = 0, depth = 1;
                while (source[source_pos] && depth > 0) {
                    if (source[source_pos] == '(') depth++;
                    else if (source[source_pos] == ')') depth--;
                    if (depth > 0 && pi < MAX_STR_LEN - 1) params[pi++] = source[source_pos];
                    source_pos++;
                }
                params[pi] = '\0';
            }
            Node* chart = alloc_node(NODE_TSCORE_CHART);
            if (!chart) return NULL;
            strncpy(chart->chart_title, "Vertebral Level T-Score Profile", sizeof(chart->chart_title) - 1);
            extract_param_value(params, "title", chart->chart_title, sizeof(chart->chart_title));

            const char* ptr = params;
            while ((ptr = strstr(ptr, "label:")) != NULL && chart->chart_count < 8) {
                ptr += 6;
                while (*ptr == ' ' || *ptr == '"' || *ptr == '\'') ptr++;
                int i = 0;
                while (*ptr && *ptr != '"' && *ptr != '\'' && *ptr != ',' && *ptr != ')' && i < 31) {
                    chart->chart_labels[chart->chart_count][i++] = *ptr++;
                }
                chart->chart_labels[chart->chart_count][i] = '\0';
                const char* s_ptr = strstr(ptr, "score:");
                if (s_ptr) {
                    s_ptr += 6;
                    while (*s_ptr == ' ' || *s_ptr == ':' || *s_ptr == '=') s_ptr++;
                    chart->chart_scores[chart->chart_count] = atof(s_ptr);
                }
                chart->chart_count++;
            }
            if (chart->chart_count == 0) {
                const char* def_lbl[] = {"L1", "L2", "L3", "L4", "L1-L4"};
                double def_sc[] = {-2.8, -2.6, -2.5, -2.4, -2.6};
                chart->chart_count = 5;
                for (int i = 0; i < 5; i++) {
                    strcpy(chart->chart_labels[i], def_lbl[i]);
                    chart->chart_scores[i] = def_sc[i];
                }
            }
            return chart;
        }

        if (strcmp(name, "bmd-trend-chart") == 0) {
            source_pos = p;
            skip_whitespace_and_comments();
            char params[MAX_STR_LEN] = {0};
            if (source[source_pos] == '(') {
                source_pos++;
                int pi = 0, depth = 1;
                while (source[source_pos] && depth > 0) {
                    if (source[source_pos] == '(') depth++;
                    else if (source[source_pos] == ')') depth--;
                    if (depth > 0 && pi < MAX_STR_LEN - 1) params[pi++] = source[source_pos];
                    source_pos++;
                }
                params[pi] = '\0';
            }
            Node* chart = alloc_node(NODE_BMD_CHART);
            if (!chart) return NULL;
            strncpy(chart->chart_title, "L1-L4 Spine BMD Trend vs Age Norms", sizeof(chart->chart_title) - 1);
            strncpy(chart->chart_v1, "0.848", sizeof(chart->chart_v1) - 1);
            strncpy(chart->chart_v2, "0.812", sizeof(chart->chart_v2) - 1);
            strncpy(chart->chart_pct, "-4.2%", sizeof(chart->chart_pct) - 1);
            extract_param_value(params, "title", chart->chart_title, sizeof(chart->chart_title));
            extract_param_value(params, "v1", chart->chart_v1, sizeof(chart->chart_v1));
            extract_param_value(params, "v2", chart->chart_v2, sizeof(chart->chart_v2));
            extract_param_value(params, "pct", chart->chart_pct, sizeof(chart->chart_pct));
            return chart;
        }
    }

    if (strncmp(&source[source_pos], "#line", 5) == 0 || strncmp(&source[source_pos], "line(", 5) == 0) return parse_line();
    if (strncmp(&source[source_pos], "#rect", 5) == 0 || strncmp(&source[source_pos], "rect(", 5) == 0 ||
        strncmp(&source[source_pos], "#box", 4) == 0  || strncmp(&source[source_pos], "box(", 4) == 0) return parse_block_node(NODE_RECT);
    if (strncmp(&source[source_pos], "#block", 6) == 0 || strncmp(&source[source_pos], "block(", 6) == 0) return parse_block_node(NODE_BLOCK);
    if (strncmp(&source[source_pos], "#grid", 5) == 0 || strncmp(&source[source_pos], "grid(", 5) == 0) return parse_table_or_grid(NODE_GRID);
    if (strncmp(&source[source_pos], "#table", 6) == 0 || strncmp(&source[source_pos], "table(", 6) == 0) return parse_table_or_grid(NODE_TABLE);

    if (strncmp(&source[source_pos], "#align(", 7) == 0 || strncmp(&source[source_pos], "align(", 6) == 0 || strncmp(&source[source_pos], "#pad(", 5) == 0) {
        Node* b = alloc_node(NODE_BLOCK);
        if (!b) return NULL;
        
        if (strncmp(&source[source_pos], "#align(", 7) == 0 || strncmp(&source[source_pos], "align(", 6) == 0) {
            char param_buf[128] = {0};
            int pi = 0;
            int tmp_pos = source_pos;
            while (source[tmp_pos] && source[tmp_pos] != ')' && source[tmp_pos] != '\n' && pi < 127) {
                param_buf[pi++] = source[tmp_pos++];
            }
            if (strstr(param_buf, "center")) b->align = 1;
            else if (strstr(param_buf, "right")) b->align = 2;
        }
        
        while (source[source_pos] && source[source_pos] != ')' && source[source_pos] != '\n') source_pos++;
        if (source[source_pos] == ')') source_pos++;
        skip_whitespace_and_comments();
        
        if (source[source_pos] == '[') {
            int start = source_pos + 1;
            int depth = 1; source_pos++;
            while (source[source_pos] && depth > 0) {
                if (source[source_pos] == '[') depth++; else if (source[source_pos] == ']') depth--;
                source_pos++;
            }
            char saved = source[source_pos - 1];
            source[source_pos - 1] = '\0';
            int saved_pos = source_pos;
            source_pos = start;
            
            while (skip_whitespace_and_comments() != 0) {
                int prev = source_pos;
                Node* elem = parse_element();
                if (elem && b->child_count < 512) {
                    elem->align = b->align;
                    b->children[b->child_count++] = elem;
                }
                if (source_pos == prev) source_pos++;
            }
            source[saved_pos - 1] = saved;
            source_pos = saved_pos;
        } else {
            int i = 0;
            while (source[source_pos] && source[source_pos] != '\n' && i < MAX_STR_LEN - 1) {
                b->content[i++] = source[source_pos++];
            }
            while (i > 0 && isspace((unsigned char)b->content[i-1])) i--;
            b->content[i] = '\0';
        }
        return b;
    }

    return parse_plain_text();
}

static Node* parse_document(void) {
    Node* doc = alloc_node(NODE_PAGE);
    if (!doc) return NULL;
    while (skip_whitespace_and_comments() != 0) {
        int prev_pos = source_pos;
        Node* elem = parse_element();
        if (elem && doc->child_count < 512) doc->children[doc->child_count++] = elem;
        if (source_pos == prev_pos) source_pos++;
        if (source[source_pos] == '\n') source_pos++;
    }
    return doc;
}

/* =========================================
   PDF ESCAPING & DRAWING PRIMITIVES
   ========================================= */

static void pdf_escape(const char* in, char* out, int max) {
    int o = 0;
    for (int i = 0; in[i] && o < max - 4; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '(' || c == ')' || c == '\\') { out[o++] = '\\'; out[o++] = c; }
        else out[o++] = c;
    }
    out[o] = '\0';
}

static void pdf_draw_text_run(StreamBuffer* sb, const char* text, double x, double y,
                              double font_size, int is_bold, int is_italic, Color text_color) {
    char sanitized[MAX_STR_LEN];
    sanitize_utf8_to_winansi(sanitized, text, sizeof(sanitized));
    char escaped[MAX_STR_LEN];
    pdf_escape(sanitized, escaped, sizeof(escaped));
    
    #ifdef _WIN32
    if (dl_count < 32768) {
        DLItem* it = &dl_items[dl_count++];
        it->type = DL_TEXT;
        it->page = current_render_page;
        it->x = x; it->y = y;
        it->font_size = font_size;
        it->is_bold = is_bold;
        it->is_italic = is_italic;
        it->c = text_color;
        it->start_angle = 0; //angle_deg; // Store angle for GDI referencing
        strncpy(it->text, text, 1023);
        it->text[1023] = '\0';
    }
    #endif
    
    const char* font = "F1";
    if (is_bold && is_italic) font = "F3";
    else if (is_bold) font = "F2";
    else if (is_italic) font = "F4";
    
    double rad = 0;//angle_deg * (3.14159265358979323846 / 180.0);
    double a = cos(rad);
    double b = sin(rad);
    double c = -sin(rad);
    double d = cos(rad);
    
    sb_printf(sb, "BT\n/%s %.1f Tf\n%.3f %.3f %.3f rg\n%.4f %.4f %.4f %.4f %.2f %.2f Tm\n(%s) Tj\nET\n0 0 0 rg\n",
              font, font_size, text_color.r / 255.0, text_color.g / 255.0, text_color.b / 255.0,
              a, b, c, d, x, y, escaped);
}

static void draw_pdf_rect(PdfContext* ctx, double x, double y, double w, double h, Color fill_c, int has_stroke, Color stroke_c, double stroke_w) {
    #ifdef _WIN32
    if (dl_count < 32768) {
        DLItem* it = &dl_items[dl_count++];
        it->type = DL_RECT;
        it->page = ctx->current_page;
        it->x = x;
        it->y = y - h;
        it->w = w;
        it->h = h;
        it->c = fill_c;
        it->stroke_c = has_stroke ? stroke_c : (Color){0,0,0,"none"};
        it->stroke_w = has_stroke ? stroke_w : 0;
    }
    #endif
    sb_printf(&ctx->pages[ctx->current_page], "%.3f %.3f %.3f rg\n", fill_c.r/255.0, fill_c.g/255.0, fill_c.b/255.0);
    sb_printf(&ctx->pages[ctx->current_page], "%.2f %.2f %.2f %.2f re f\n", x, y - h, w, h);
    if (has_stroke) {
        sb_printf(&ctx->pages[ctx->current_page], "%.3f %.3f %.3f RG %.2f w\n", stroke_c.r/255.0, stroke_c.g/255.0, stroke_c.b/255.0, stroke_w);
        sb_printf(&ctx->pages[ctx->current_page], "%.2f %.2f %.2f %.2f re S\n0 0 0 RG\n", x, y - h, w, h);
    }
}

static void draw_pdf_line(PdfContext* ctx, double x1, double y1, double x2, double y2, Color stroke_c, double stroke_w) {
    #ifdef _WIN32
    if (dl_count < 32768) {
        DLItem* it = &dl_items[dl_count++];
        it->type = DL_LINE;
        it->page = ctx->current_page;
        it->x = x1;
        it->y = y1;
        it->w = x2 - x1;
        it->h = y2 - y1;
        it->c = stroke_c;
        it->stroke_w = stroke_w;
    }
    #endif
    sb_printf(&ctx->pages[ctx->current_page], "%.3f %.3f %.3f RG %.2f w\n", stroke_c.r/255.0, stroke_c.g/255.0, stroke_c.b/255.0, stroke_w);
    sb_printf(&ctx->pages[ctx->current_page], "%.2f %.2f m %.2f %.2f l S\n0 0 0 RG\n", x1, y1, x2, y2);
}

/* =========================================
   TEXT LAYOUT & PRECISE MEASUREMENT
   ========================================= */

static double get_char_width(char c, double font_size, int is_bold) {
    static const unsigned short w[256] = {
        [' '] = 278, ['!'] = 278, ['"'] = 355, ['#'] = 556, ['$'] = 556, ['%'] = 889, ['&'] = 667, ['\''] = 191,
        ['('] = 333, [')'] = 333, ['*'] = 389, ['+'] = 584, [','] = 278, ['-'] = 333, ['.'] = 278, ['/'] = 278,
        ['0'] = 556, ['1'] = 556, ['2'] = 556, ['3'] = 556, ['4'] = 556, ['5'] = 556, ['6'] = 556, ['7'] = 556,
        ['8'] = 556, ['9'] = 556, [':'] = 278, [';'] = 278, ['<'] = 584, ['='] = 584, ['>'] = 584, ['?'] = 556,
        ['@'] = 1015, ['A'] = 667, ['B'] = 667, ['C'] = 722, ['D'] = 722, ['E'] = 667, ['F'] = 611, ['G'] = 778, ['H'] = 722,
        ['I'] = 278, ['J'] = 500, ['K'] = 667, ['L'] = 556, ['M'] = 833, ['N'] = 722, ['O'] = 778, ['P'] = 667,
        ['Q'] = 778, ['R'] = 722, ['S'] = 667, ['T'] = 611, ['U'] = 722, ['V'] = 667, ['W'] = 944, ['X'] = 667,
        ['Y'] = 667, ['Z'] = 611, ['['] = 278, ['\\'] = 278, [']'] = 278, ['^'] = 469, ['_'] = 556, ['`'] = 222,
        ['a'] = 556, ['b'] = 556, ['c'] = 500, ['d'] = 556, ['e'] = 556, ['f'] = 278, ['g'] = 556, ['h'] = 556,
        ['i'] = 222, ['j'] = 222, ['k'] = 500, ['l'] = 222, ['m'] = 833, ['n'] = 556, ['o'] = 556, ['p'] = 556,
        ['q'] = 556, ['r'] = 333, ['s'] = 500, ['t'] = 278, ['u'] = 556, ['v'] = 500, ['w'] = 722, ['x'] = 500,
        ['y'] = 500, ['z'] = 500, ['{'] = 333, ['|'] = 260, ['}'] = 333, ['~'] = 584,
        [0x95] = 350, // Bullet (•)
        [0x99] = 600, // Trademark (™)
        [150]  = 500, // En-dash (–)
        [151]  = 1000,// Em-dash (—)
        [169]  = 600, // Copyright (©)
        [174]  = 600, // Registered (®)
        [176]  = 333, // Degree (°)
        [178]  = 333, // Superscript 2 (²)
        [183]  = 278  // Middle dot (·)
    };
    unsigned char uc = (unsigned char)c;
    double width = (w[uc] > 0 ? w[uc] : 500) / 1000.0 * font_size;
    if (is_bold) width *= 1.05; 
    return width;
}

static double render_styled_text(StreamBuffer* sb, const char* text, double x, double y,
                                 double max_w, double font_size, int align, Color default_color, int dry_run) {
    TextRun* runs = (TextRun*)malloc(sizeof(TextRun) * MAX_RUNS);
    if (!runs) return 0;

    int run_count = 0;
    TextState base_state = {font_size, default_color, {0,0,0,""}, 0, 0};
    parse_inline_runs(text, base_state, runs, &run_count);

    double max_run_size = font_size;
    for (int i = 0; i < run_count; i++) {
        if (runs[i].font_size > max_run_size) max_run_size = runs[i].font_size;
    }

    double draw_y_top = y;
    double current_y = draw_y_top - max_run_size;
    double start_x = x;

    double total_est_w = 0;
    if (align > 0) {
        for (int i = 0; i < run_count; i++) {
            const char* p = runs[i].text;
            while (*p) {
                if (*p != '\n') total_est_w += get_char_width(*p, runs[i].font_size, runs[i].is_bold);
                p++;
            }
        }
        if (total_est_w > 0 && total_est_w < max_w) {
            if (align == 1) start_x += (max_w - total_est_w) / 2.0;
            else if (align == 2) start_x += (max_w - total_est_w);
        }
    }

    double draw_x = start_x;

    for (int i = 0; i < run_count; i++) {
        if (strcmp(runs[i].text, "\n") == 0) {
            current_y -= (max_run_size + 4.0);
            draw_x = start_x;
            continue;
        }

        char word[1024];
        int wi = 0;
        const char* p = runs[i].text;
        
        char line_buf[4096] = {0};
        double frag_start_x = draw_x;

        while (*p) {
            if (*p == ' ' || *p == '\t') {
                word[wi] = '\0';
                double word_w = 0;
                for (int k = 0; k < wi; k++) word_w += get_char_width(word[k], runs[i].font_size, runs[i].is_bold);
                double space_w = get_char_width(' ', runs[i].font_size, runs[i].is_bold);
                
                if (wi > 0 && draw_x + word_w > start_x + max_w && draw_x > start_x) {
                    if (line_buf[0] != '\0') {
                        if (!dry_run && sb) pdf_draw_text_run(sb, line_buf, frag_start_x, current_y, runs[i].font_size, runs[i].is_bold, runs[i].is_italic, runs[i].color);
                        line_buf[0] = '\0';
                    }
                    current_y -= (max_run_size + 4.0);
                    draw_x = start_x;
                    frag_start_x = start_x;
                }
                
                if (wi > 0) {
                    strcat(line_buf, word);
                    draw_x += word_w;
                }
                strcat(line_buf, " ");
                draw_x += space_w;
                wi = 0;
                p++;
            } else {
                if (wi < 1023) word[wi++] = *p;
                p++;
            }
        }
        
        if (wi > 0) {
            word[wi] = '\0';
            double word_w = 0;
            for (int k = 0; k < wi; k++) word_w += get_char_width(word[k], runs[i].font_size, runs[i].is_bold);
            
            if (draw_x + word_w > start_x + max_w && draw_x > start_x) {
                if (line_buf[0] != '\0') {
                    if (!dry_run && sb) pdf_draw_text_run(sb, line_buf, frag_start_x, current_y, runs[i].font_size, runs[i].is_bold, runs[i].is_italic, runs[i].color);
                    line_buf[0] = '\0';
                }
                current_y -= (max_run_size + 4.0);
                draw_x = start_x;
                frag_start_x = start_x;
            }
            strcat(line_buf, word);
            draw_x += word_w;
        }
        
        if (line_buf[0] != '\0') {
            if (!dry_run && sb) pdf_draw_text_run(sb, line_buf, frag_start_x, current_y, runs[i].font_size, runs[i].is_bold, runs[i].is_italic, runs[i].color);
        }
    }

    double total_height = (draw_y_top - current_y) + 4.0;
    free(runs);
    return total_height;
}

static double measure_node_height(Node* n, double max_w) {
    if (!n) return 0;
    switch (n->type) {
        case NODE_VSPACE: return n->height;
        case NODE_LINE: return 10.0;
        case NODE_PARAGRAPH:
        case NODE_HEADING: {
            return render_styled_text(NULL, n->content, 0, 0, max_w, n->font_size, n->align, current_state.fill_color, 1);
        }
        case NODE_BLOCK: {
            double bw = max_w;
            if (n->has_width) {
                bw = (n->has_width == 2) ? n->width * max_w : n->width;
            }
            if (n->child_count > 0) {
                double h = 0;
                for (int i = 0; i < n->child_count; i++) h += measure_node_height(n->children[i], bw);
                return h;
            }
            return render_styled_text(NULL, n->content, 0, 0, bw, n->font_size, n->align, current_state.fill_color, 1);
        }
        case NODE_RECT: {
            double rw = max_w;
            if (n->has_width) {
                rw = (n->has_width == 2) ? n->width * max_w : n->width;
            }
            double inset = n->inset > 0 ? n->inset : 8.0;
            double content_h = 0;
            if (n->child_count > 0) {
                for (int i = 0; i < n->child_count; i++) content_h += measure_node_height(n->children[i], rw - inset*2);
            } else {
                content_h = render_styled_text(NULL, n->content, 0, 0, rw - inset*2, n->font_size, 0, current_state.fill_color, 1);
            }
            return content_h + inset * 2.0;
        }
        case NODE_TABLE: {
            double total_fixed = 0, total_fr = 0;
            for (int i = 0; i < n->cell_cols; i++) {
                if (n->is_fr[i]) total_fr += n->col_widths[i];
                else total_fixed += n->col_widths[i];
            }
            double avail = max_w - total_fixed;
            double fr_unit = total_fr > 0 ? avail / total_fr : 0;
            double actual_widths[MAX_TABLE_COLS];
            for (int i = 0; i < n->cell_cols; i++) {
                actual_widths[i] = n->is_fr[i] ? n->col_widths[i] * fr_unit : n->col_widths[i];
            }
            double inset = n->inset > 0 ? n->inset : 6.0;
            double total_h = 0;
            for (int r = 0; r < n->cell_rows; r++) {
                double max_row_h = 0;
                for (int c = 0; c < n->cell_cols; c++) {
                    Node* cell = n->cells[r][c];
                    if (cell) {
                        double ch = 0;
                        if (cell->child_count > 0) {
                            for (int k = 0; k < cell->child_count; k++) ch += measure_node_height(cell->children[k], actual_widths[c] - inset*2);
                        } else {
                            ch = render_styled_text(NULL, cell->content, 0, 0, actual_widths[c] - inset*2, n->font_size, cell->align, current_state.fill_color, 1);
                        }
                        max_row_h = MAX(max_row_h, ch);
                    }
                }
                total_h += max_row_h + inset * 2.0;
            }
            return total_h;
        }
        case NODE_GRID: {
            double total_fixed = 0, total_fr = 0;
            for (int i = 0; i < n->cell_cols; i++) {
                if (n->is_fr[i]) total_fr += n->col_widths[i];
                else total_fixed += n->col_widths[i];
            }
            double avail = max_w - total_fixed - (n->cell_cols - 1) * n->gutter;
            double fr_unit = total_fr > 0 ? avail / total_fr : 0;
            double actual_widths[MAX_TABLE_COLS];
            for (int i = 0; i < n->cell_cols; i++) {
                actual_widths[i] = n->is_fr[i] ? n->col_widths[i] * fr_unit : n->col_widths[i];
            }
            double total_h = 0;
            for (int r = 0; r < n->cell_rows; r++) {
                double max_row_h = 0;
                for (int c = 0; c < n->cell_cols; c++) {
                    Node* cell = n->cells[r][c];
                    if (cell) {
                        double ch = 0;
                        if (cell->child_count > 0) {
                            for (int k = 0; k < cell->child_count; k++) ch += measure_node_height(cell->children[k], actual_widths[c]);
                        } else {
                            ch = render_styled_text(NULL, cell->content, 0, 0, actual_widths[c], n->font_size, cell->align, current_state.fill_color, 1);
                        }
                        max_row_h = MAX(max_row_h, ch);
                    }
                }
                total_h += max_row_h;
            }
            return total_h;
        }
        case NODE_PIE_CHART: 
            return 260.0;
        case NODE_LINE_CHART:
            return 225.0;
        case NODE_BAR_CHART: {
            double base_h = (n->chart_x_label[0] != '\0') ? 225.0 : 200.0;
            return (n->chart_title[0] != '\0') ? base_h + 25.0 : base_h;}
        case NODE_TSCORE_CHART:
        case NODE_BMD_CHART:
            return 105.0;
        default: return 16.0;
    }
}

// Translates raw UTF-8 sequences to WinAnsi codes for PDF compatibility
void sanitize_utf8_to_winansi(char* dest, const char* src, size_t max_len) {
    size_t i = 0, j = 0;
    while (src && src[i] && j < max_len - 1) {
        // Handle Em-dash and En-dash
        if ((unsigned char)src[i] == 0xE2 && (unsigned char)src[i+1] == 0x80) {
            if ((unsigned char)src[i+2] == 0x93) { dest[j++] = (char)150; i+=3; continue; } // – (En-dash)
            if ((unsigned char)src[i+2] == 0x94) { dest[j++] = (char)151; i+=3; continue; } // — (Em-dash)
        }
        // Handle Trademark (™ = E2 84 A2)
        if ((unsigned char)src[i] == 0xE2 && (unsigned char)src[i+1] == 0x84 && (unsigned char)src[i+2] == 0xA2) {
            dest[j++] = (char)153; i+=3; continue; // ™ (Trademark)
        }
        // Handle cm², degrees, copyright, registered
        if ((unsigned char)src[i] == 0xC2) {
            if ((unsigned char)src[i+1] == 0xB2) { dest[j++] = (char)178; i+=2; continue; } // ²
            if ((unsigned char)src[i+1] == 0xB0) { dest[j++] = (char)176; i+=2; continue; } // °
            if ((unsigned char)src[i+1] == 0xA9) { dest[j++] = (char)169; i+=2; continue; } // ©
            if ((unsigned char)src[i+1] == 0xAE) { dest[j++] = (char)174; i+=2; continue; } // ®
        }
        dest[j++] = src[i++];
    }
    dest[j] = '\0';
}

/* =========================================
   NODE RENDERER
   ========================================= */

static void render_node(PdfContext* ctx, Node* n, double* y, double max_w, double start_x) {
    if (!n) return;
    
    if (n->content) {
        size_t clen = strlen(n->content);
        if (clen > 0 && n->content[clen - 1] == ']') {
            n->content[clen - 1] = '\0';
        }
    }
    
    if (n->type == NODE_PAGEBREAK) {
        if (ctx->current_page < MAX_PAGES - 1) {
            ctx->current_page++;
            #ifdef _WIN32
            current_render_page = ctx->current_page;
            if (current_render_page > max_page_num) max_page_num = current_render_page;
            #endif
            sb_init(&ctx->pages[ctx->current_page]);
        }
        *y = page_height - margin_top;
        return;
    }

    if (*y < margin_bottom + 20.0) {
        // 1. Check if we are allowed to break the page
        if (!ctx->disable_pagebreaks) {
            if (ctx->current_page < MAX_PAGES - 1) {
                ctx->current_page++;
                
                #ifdef _WIN32
                current_render_page = ctx->current_page;
                if (current_render_page > max_page_num) max_page_num = current_render_page;
                #endif
                
                sb_init(&ctx->pages[ctx->current_page]); // Init new page
            }
            
            // Reset Y to the top of the new page
            *y = page_height - margin_top; 
        }
        // 2. If disable_pagebreaks IS true, 
        // we just bypass this entirely and let it keep rendering where it is.
    }

    #define CUR_SB (&ctx->pages[ctx->current_page])

    double render_w = n->has_width ? n->width : max_w;
    double current_x = start_x;

    switch (n->type) {
case NODE_PAGE: {
    // 1. Render all children of the document onto the pages
    for (int i = 0; i < n->child_count; i++) {
        render_node(ctx, n->children[i], y, max_w, start_x);
    }

    // 2. Overlay Headers and Footers for all generated pages
    int total_pages = ctx->current_page + 1;
    int saved_page = ctx->current_page;
    TextState saved_state = current_state;

    for (int p = 0; p < total_pages; p++) {
        ctx->current_page = p;
        #ifdef _WIN32
        current_render_page = p; // Keep GDI text layout in sync
        #endif

        if (header_script[0] != '\0') {
            char fmt[MAX_STR_LEN];
            char* dst = fmt;
            const char* src = header_script;
            size_t rem = sizeof(fmt) - 1;
            
            while (*src && rem > 0) {
                if (strncmp(src, "#counter(page).display()", 24) == 0) {
                    char num[16];
                    snprintf(num, sizeof(num), "%d", p + 1);
                    size_t nlen = strlen(num);
                    if (nlen <= rem) { strcpy(dst, num); dst += nlen; rem -= nlen; }
                    src += 24;
                } else if (strncmp(src, "counter(page).display()", 23) == 0) {
                    char num[16];
                    snprintf(num, sizeof(num), "%d", p + 1);
                    size_t nlen = strlen(num);
                    if (nlen <= rem) { strcpy(dst, num); dst += nlen; rem -= nlen; }
                    src += 23;
                } else if (*src == '%' && *(src+1) == 'd') {
                    char num[16];
                    snprintf(num, sizeof(num), "%d", p + 1);
                    size_t nlen = strlen(num);
                    if (nlen <= rem) { strcpy(dst, num); dst += nlen; rem -= nlen; }
                    src += 2;
                } else {
                    *dst++ = *src++;
                    rem--;
                }
            }
            *dst = '\0';
            
            Node* h = parse_typst_string(fmt);
            if (h) {
                double hy = page_height - (margin_top / 2.0);
                int old_disable = ctx->disable_pagebreaks;
                ctx->disable_pagebreaks = 1;
                render_node(ctx, h, &hy, max_w, start_x);
                ctx->disable_pagebreaks = old_disable;
            }
        }

        if (footer_script[0] != '\0') {
            char fmt[MAX_STR_LEN];
            char* dst = fmt;
            const char* src = footer_script;
            size_t rem = sizeof(fmt) - 1;
            
            while (*src && rem > 0) {
                if (strncmp(src, "#counter(page).display()", 24) == 0) {
                    char num[16];
                    snprintf(num, sizeof(num), "%d", p + 1);
                    size_t nlen = strlen(num);
                    if (nlen <= rem) { strcpy(dst, num); dst += nlen; rem -= nlen; }
                    src += 24;
                } else if (strncmp(src, "counter(page).display()", 23) == 0) {
                    char num[16];
                    snprintf(num, sizeof(num), "%d", p + 1);
                    size_t nlen = strlen(num);
                    if (nlen <= rem) { strcpy(dst, num); dst += nlen; rem -= nlen; }
                    src += 23;
                } else if (*src == '%' && *(src+1) == 'd') {
                    char num[16];
                    snprintf(num, sizeof(num), "%d", p + 1);
                    size_t nlen = strlen(num);
                    if (nlen <= rem) { strcpy(dst, num); dst += nlen; rem -= nlen; }
                    src += 2;
                } else {
                    *dst++ = *src++;
                    rem--;
                }
            }
            *dst = '\0';
            
            Node* f = parse_typst_string(fmt);
            if (f) {
                double fy = margin_bottom / 2.0;
                int old_disable = ctx->disable_pagebreaks;
                ctx->disable_pagebreaks = 1;
                render_node(ctx, f, &fy, max_w, start_x);
                ctx->disable_pagebreaks = old_disable;
            }
        }
    }

    // 3. Restore engine state
    ctx->current_page = saved_page;
    #ifdef _WIN32
    current_render_page = saved_page;
    #endif
    current_state = saved_state;
    break;
}
        case NODE_HEADING: {
            char sanitized[512];
            sanitize_utf8_to_winansi(sanitized, n->content, sizeof(sanitized));
            double h = render_styled_text(CUR_SB, sanitized, current_x, *y, render_w, n->font_size, n->align, get_color("#2B6CB0"), 0);
            *y -= (h + 8.0);
            break;
        }
        case NODE_PARAGRAPH: {
            char sanitized[1024];
            sanitize_utf8_to_winansi(sanitized, n->content, sizeof(sanitized));
            double h = render_styled_text(CUR_SB, sanitized, current_x, *y, render_w, n->font_size, n->align, current_state.fill_color, 0);
            *y -= h;
            break;
        }
        case NODE_BLOCK: {
            double bw = max_w;
            if (n->has_width) {
                bw = (n->has_width == 2) ? n->width * max_w : n->width;
            }
            double bx = current_x;
            if (n->align == 1) bx = start_x + (max_w - bw) / 2.0;
            else if (n->align == 2) bx = start_x + (max_w - bw);

            for (int i = 0; i < n->child_count; i++) {
                render_node(ctx, n->children[i], y, bw, bx);
            }
            break;
        }
        case NODE_LINE: {
            double lw = max_w;
            if (n->has_width) {
                lw = (n->has_width == 2) ? n->width * max_w : n->width;
            }
            Color lc = n->has_stroke ? n->stroke_color : get_color("#CBD5E0");
            double sw = n->stroke_width > 0 ? n->stroke_width : 0.5;
            
            #ifdef _WIN32
            if (dl_count < 32768) {
                DLItem* it = &dl_items[dl_count++];
                it->type = DL_LINE;
                it->page = ctx->current_page;
                it->x = current_x; it->y = *y;
                it->w = lw; it->h = 0;
                it->c = lc; it->stroke_w = sw;
            }
            #endif
            
            sb_printf(CUR_SB, "%.3f %.3f %.3f RG %.2f w\n", lc.r/255.0, lc.g/255.0, lc.b/255.0, sw);
            sb_printf(CUR_SB, "%.2f %.2f m %.2f %.2f l S\n0 0 0 RG\n", current_x, *y, current_x + lw, *y);
            *y -= 8.0;
            break;
        }
        case NODE_VSPACE: {
            *y -= n->height;
            break;
        }
        case NODE_RECT: {
            double rw = max_w;
            if (n->has_width) {
                rw = (n->has_width == 2) ? n->width * max_w : n->width;
            }
            double inset = n->inset > 0 ? n->inset : 8.0;
            double content_h = 0;
            if (n->child_count > 0) {
                for (int i = 0; i < n->child_count; i++) content_h += measure_node_height(n->children[i], rw - inset*2);
            } else if (n->content && strlen(n->content) > 0) {
                char sanitized[512];
                sanitize_utf8_to_winansi(sanitized, n->content, sizeof(sanitized));
                content_h = render_styled_text(NULL, sanitized, 0, 0, rw - inset*2, n->font_size, 0, current_state.fill_color, 1);
            } else {
                content_h = 10.0;
            }
            double total_h = content_h + inset * 2.0;

            int has_fill = (n->fill_color.name && strcmp(n->fill_color.name, "none") != 0 && strcmp(n->fill_color.name, "transparent") != 0);

            // STRICT DISPATCH: Only use rounded rects if radius is explicitly > 0.5
            if (n->radius > 0.5) {
                draw_pdf_rounded_rect(ctx, current_x, *y, rw, total_h, n->radius, n->fill_color, has_fill, n->has_stroke, n->stroke_color, n->stroke_width);
            } else {
                // FIXED: Arg 7 is 'n->has_stroke', NOT 'has_fill'!
                draw_pdf_rect(ctx, current_x, *y, rw, total_h, n->fill_color, n->has_stroke, n->stroke_color, n->has_stroke ? n->stroke_width : 0.0);
            }

            if (n->child_count > 0) {
                double child_y = *y - inset;
                for (int i = 0; i < n->child_count; i++) render_node(ctx, n->children[i], &child_y, rw - inset*2, current_x + inset);
            } else if (n->content && strlen(n->content) > 0) {
                char sanitized[512];
                sanitize_utf8_to_winansi(sanitized, n->content, sizeof(sanitized));
                render_styled_text(CUR_SB, sanitized, current_x + inset, *y - inset, rw - inset*2, n->font_size, 0, current_state.fill_color, 0);
            }
            *y -= (total_h + 8.0);
            break;
        }
        case NODE_GRID: {
            double total_fixed = 0, total_fr = 0;
            for (int i = 0; i < n->cell_cols; i++) {
                if (n->is_fr[i]) total_fr += n->col_widths[i];
                else total_fixed += n->col_widths[i];
            }
            double avail = render_w - total_fixed - (n->cell_cols - 1) * n->gutter;
            double fr_unit = total_fr > 0 ? avail / total_fr : 0;
            double actual_widths[MAX_TABLE_COLS];
            for (int i = 0; i < n->cell_cols; i++) {
                actual_widths[i] = n->is_fr[i] ? n->col_widths[i] * fr_unit : n->col_widths[i];
            }

            for (int r = 0; r < n->cell_rows; r++) {
                double cx = current_x;
                double max_row_h = 0;

                for (int c = 0; c < n->cell_cols; c++) {
                    Node* cell = n->cells[r][c];
                    if (cell) {
                        double ch = 0;
                        if (cell->child_count > 0) {
                            for (int k = 0; k < cell->child_count; k++) ch += measure_node_height(cell->children[k], actual_widths[c]);
                        } else {
                            char sanitized[512];
                            sanitize_utf8_to_winansi(sanitized, cell->content, sizeof(sanitized));
                            ch = render_styled_text(NULL, sanitized, 0, 0, actual_widths[c], n->font_size, cell->align, current_state.fill_color, 1);
                        }
                        max_row_h = MAX(max_row_h, ch);
                    }
                }

                for (int c = 0; c < n->cell_cols; c++) {
                    Node* cell = n->cells[r][c];
                    if (cell) {
                        if (cell->child_count > 0) {
                            double child_y = *y;
                            for (int k = 0; k < cell->child_count; k++) render_node(ctx, cell->children[k], &child_y, actual_widths[c], cx);
                        } else {
                            char sanitized[512];
                            sanitize_utf8_to_winansi(sanitized, cell->content, sizeof(sanitized));
                            render_styled_text(CUR_SB, sanitized, cx, *y, actual_widths[c], n->font_size, cell->align, current_state.fill_color, 0);
                        }
                    }
                    cx += actual_widths[c] + n->gutter;
                }
                *y -= max_row_h;
            }
            break;
        }
        case NODE_TABLE: {
            double total_fixed = 0, total_fr = 0;
            for (int i = 0; i < n->cell_cols; i++) {
                if (n->is_fr[i]) total_fr += n->col_widths[i];
                else total_fixed += n->col_widths[i];
            }
            double avail = render_w - total_fixed;
            double fr_unit = total_fr > 0 ? avail / total_fr : 0;
            double actual_widths[MAX_TABLE_COLS];
            double total_table_width = 0;
            for (int i = 0; i < n->cell_cols; i++) {
                actual_widths[i] = n->is_fr[i] ? n->col_widths[i] * fr_unit : n->col_widths[i];
                total_table_width += actual_widths[i];
            }

            double inset = n->inset > 0 ? n->inset : 6.0;
            double start_y = *y;
            
            double* row_y_positions = (double*)malloc((n->cell_rows + 1) * sizeof(double));
            if (row_y_positions) row_y_positions[0] = start_y;

            for (int r = 0; r < n->cell_rows; r++) {
                double max_row_h = 0;
                for (int c = 0; c < n->cell_cols; c++) {
                    Node* cell = n->cells[r][c];
                    if (cell) {
                        double ch = 0;
                        if (cell->child_count > 0) {
                            for (int k = 0; k < cell->child_count; k++) ch += measure_node_height(cell->children[k], actual_widths[c] - inset*2);
                        } else {
                            char sanitized[512];
                            sanitize_utf8_to_winansi(sanitized, cell->content, sizeof(sanitized));
                            ch = render_styled_text(NULL, sanitized, 0, 0, actual_widths[c] - inset*2, n->font_size, cell->align, current_state.fill_color, 1);
                        }
                        max_row_h = MAX(max_row_h, ch);
                    }
                }
                double cell_box_h = max_row_h + inset * 2.0;

                Color row_fill;
                if (r == 0 && n->alt_rows) {
                    row_fill = n->header_fill;
                } else if (n->alt_rows) {
                    row_fill = (r % 2 == 1) ? n->stripe_fill_1 : n->stripe_fill_2;
                } else {
                    row_fill = n->fill_color;
                }

                double cx = current_x;
                for (int c = 0; c < n->cell_cols; c++) {
                    Node* cell = n->cells[r][c];
                    
                    #ifdef _WIN32
                    if (dl_count < 32768) {
                        DLItem* it = &dl_items[dl_count++];
                        it->type = DL_RECT;
                        it->page = ctx->current_page;
                        it->x = cx; it->y = *y - cell_box_h;
                        it->w = actual_widths[c]; it->h = cell_box_h;
                        it->c = row_fill;
                        it->stroke_c = (Color){0,0,0,"none"};
                        it->stroke_w = 0;
                    }
                    #endif

                    sb_printf(CUR_SB, "%.3f %.3f %.3f rg\n", row_fill.r/255.0, row_fill.g/255.0, row_fill.b/255.0);
                    sb_printf(CUR_SB, "%.2f %.2f %.2f %.2f re f\n", cx, *y - cell_box_h, actual_widths[c], cell_box_h);

                    if (cell) {
                        Color tc = (r == 0 && n->alt_rows) ? (Color){255,255,255,"white"} : current_state.fill_color;
                        if (cell->child_count > 0) {
                            Color old_color = current_state.fill_color;
                            current_state.fill_color = tc;
                            double child_y = *y - inset;
                            for (int k = 0; k < cell->child_count; k++) render_node(ctx, cell->children[k], &child_y, actual_widths[c] - inset*2, cx + inset);
                            current_state.fill_color = old_color;
                        } else {
                            char sanitized[512];
                            sanitize_utf8_to_winansi(sanitized, cell->content, sizeof(sanitized));
                            render_styled_text(CUR_SB, sanitized, cx + inset, *y - inset, actual_widths[c] - inset*2, n->font_size, cell->align, tc, 0);
                        }
                    }
                    cx += actual_widths[c];
                }
                *y -= cell_box_h;
                
                if (row_y_positions) row_y_positions[r + 1] = *y;
            }
            
            if (n->has_stroke && row_y_positions) {
                sb_printf(CUR_SB, "%.3f %.3f %.3f RG %.2f w\n", 
                          n->stroke_color.r/255.0, n->stroke_color.g/255.0, n->stroke_color.b/255.0, n->stroke_width);
                sb_printf(CUR_SB, "%.2f %.2f %.2f %.2f re S\n", 
                          current_x, *y, total_table_width, start_y - *y);

                for (int r = 1; r < n->cell_rows; r++) {
                    double ry = row_y_positions[r];
                    sb_printf(CUR_SB, "%.2f %.2f m %.2f %.2f l S\n", current_x, ry, current_x + total_table_width, ry);
                }
                
                double cx = current_x;
                for (int c = 1; c < n->cell_cols; c++) {
                    cx += actual_widths[c - 1];
                    sb_printf(CUR_SB, "%.2f %.2f m %.2f %.2f l S\n", cx, start_y, cx, *y);
                }
                sb_printf(CUR_SB, "0 0 0 RG\n");
            }
            
            if (row_y_positions) free(row_y_positions);
            break;
        }
case NODE_BAR_CHART: {
    int has_x_label = (n->chart_x_label[0] != '\0');
    int has_y_label = (n->chart_y_label[0] != '\0');
    int has_title   = (n->chart_title[0] != '\0');
    
    double h = has_x_label ? 225.0 : 200.0;
    double w = 440.0;
    
    // Match the layout parameters exactly
    double x_start = has_y_label ? 65.0 : 50.0;
    double x_end = 415.0;
    double plot_width = x_end - x_start;
    double y_top = 20.0;
    double y_bottom = 150.0;
    double plot_height = y_bottom - y_top;
    
    // Center the 440px wide chart within the available document render width
    double ox = current_x + (render_w - w) / 2.0;
    if (ox < current_x) ox = current_x; 
    
    double oy = *y; 
    
    Color text_c = {102, 102, 102, "gray_666"};
    Color title_c = {0, 0, 0, "black"};
    Color axis_c = {51, 51, 51, "gray_333"};
    Color grid_c = {229, 231, 235, "gray_e5e7eb"};
    
    if (has_title) {
        double tw = 0;
        for(int k=0; n->chart_title[k]; k++) tw += get_char_width(n->chart_title[k], 12.0, 1);
        pdf_draw_text_run(CUR_SB, n->chart_title, ox + (w - tw)/2.0, oy - 12.0, 12.0, 1, 0, title_c);
        oy -= 25.0; // Spacing to separate the title from the grid
    }
    
    // Calculate the ceiling maximum value
    double raw_max = 0;
    for (int i = 0; i < n->chart_count; i++) {
        if (n->chart_scores[i] > raw_max) raw_max = n->chart_scores[i];
    }
    double max_val = raw_max == 0 ? 10.0 : ceil(raw_max / 10.0) * 10.0;
    if (max_val < 10.0) max_val = 10.0;
    
    // Draw the 4 background grid intervals and 5 Y-axis scale labels
    for (int idx = 0; idx < 5; idx++) {
        double val = (max_val / 4.0) * (4 - idx);
        double ly = oy - (y_top + (idx * (plot_height / 4.0)));
        
        if (idx < 4) { 
            draw_pdf_line(ctx, ox + x_start, ly, ox + x_end, ly, grid_c, 1.0);
        }
        
        char lbuf[32];
        snprintf(lbuf, sizeof(lbuf), "%.0f", val);
        double lw = 0; for(int k=0; lbuf[k]; k++) lw += get_char_width(lbuf[k], 10.0, 0);
        pdf_draw_text_run(CUR_SB, lbuf, ox + x_start - 8.0 - lw, ly - 3.0, 10.0, 0, 0, text_c);
    }
    
    // Draw the primary X and Y bounding axes
    draw_pdf_line(ctx, ox + x_start, oy - y_bottom, ox + x_end, oy - y_bottom, axis_c, 2.0); 
    draw_pdf_line(ctx, ox + x_start, oy - y_top, ox + x_start, oy - y_bottom, axis_c, 2.0); 
    
    // Draw the active bars
    if (n->chart_count > 0) {
        double slot_width = plot_width / n->chart_count;
        double bar_width = slot_width * 0.7;
        if (bar_width > 35.0) bar_width = 35.0;
        
        const char* palette[] = {"#2563eb", "#16a34a", "#d97706", "#dc2626", "#7c3aed", "#06b6d4", "#db2777", "#4f46e5"};
        int is_random = (strcmp(n->chart_color, "random") == 0);
        Color default_bar_c = get_color(n->chart_color);
        
        for (int i = 0; i < n->chart_count; i++) {
            double slot_center = x_start + (i * slot_width) + (slot_width / 2.0);
            double bx = slot_center - (bar_width / 2.0);
            double b_h = (n->chart_scores[i] / max_val) * plot_height;
            double by = oy - y_bottom + b_h;
            
            Color bar_c = is_random ? get_color(palette[i % 8]) : default_bar_c;
            
            if (b_h > 0) {
                draw_pdf_rect(ctx, ox + bx, by, bar_width, b_h, bar_c, 0, bar_c, 0);
            }
            
            // Draw numerical values above each bar
            char vbuf[32];
            snprintf(vbuf, sizeof(vbuf), "%.0f", n->chart_scores[i]);
            double vw = 0; for(int k=0; vbuf[k]; k++) vw += get_char_width(vbuf[k], 10.0, 0);
            pdf_draw_text_run(CUR_SB, vbuf, ox + slot_center - (vw/2.0), by + 6.0, 10.0, 0, 0, bar_c);
            
            // Draw specific X-axis data labels below each bar
            double lw = 0; for(int k=0; n->chart_labels[i][k]; k++) lw += get_char_width(n->chart_labels[i][k], 11.0, 0);
            pdf_draw_text_run(CUR_SB, n->chart_labels[i], ox + slot_center - (lw/2.0), oy - y_bottom - 20.0, 11.0, 0, 0, text_c);
        }
    }
    
    // Draw overarching Y-axis label 
    if (has_y_label) {
        double cy = oy - (y_top + (plot_height / 2.0));
        double tw = 0; for(int k=0; n->chart_y_label[k]; k++) tw += get_char_width(n->chart_y_label[k], 11.0, 0);
        pdf_draw_text_run(CUR_SB, n->chart_y_label, ox + 20.0, cy - (tw/2.0), 11.0, 0, 0, (Color){51,51,51,"#333"});
    }
    
    // Draw overarching X-axis label
    if (has_x_label) {
        double cx = x_start + (plot_width / 2.0);
        double tw = 0; for(int k=0; n->chart_x_label[k]; k++) tw += get_char_width(n->chart_x_label[k], 11.0, 0);
        pdf_draw_text_run(CUR_SB, n->chart_x_label, ox + cx - (tw/2.0), oy - y_bottom - 42.0, 11.0, 0, 0, (Color){51,51,51,"#333"});
    }
    
    *y -= (has_title ? h + 25.0 : h);
    break;
}
case NODE_PIE_CHART: {
    double ch = 260.0;
    double cx = current_x + 140.0;
    double cy_center = *y - 120.0;
    double r = 85.0;

    if (n->chart_title[0] != '\0') {
        char sanitized[128];
        sanitize_utf8_to_winansi(sanitized, n->chart_title, sizeof(sanitized));
        render_styled_text(CUR_SB, sanitized, current_x, *y - 15.0, render_w, n->font_size + 4.0, 1, current_state.fill_color, 0);
    }

    Color palette[8] = {
        {37, 99, 235, "blue"}, {22, 163, 74, "green"}, {217, 119, 6, "orange"}, {220, 38, 38, "red"},
        {124, 58, 237, "purple"}, {6, 182, 212, "cyan"}, {219, 39, 119, "pink"}, {79, 70, 229, "indigo"}
    };

    double total = 0;
    for (int i = 0; i < n->chart_count; i++) total += n->chart_scores[i];
    if (total == 0) total = 1;

    double start_angle = -90.0 * (M_PI / 180.0);

    for (int i = 0; i < n->chart_count; i++) {
        double slice_angle = (n->chart_scores[i] / total) * 2.0 * M_PI;
        if (slice_angle < 0.001) continue; 
        
        double end_angle = start_angle + slice_angle;
        Color col = palette[i % 8];

        #ifdef _WIN32
        if (dl_count < 32768) {
            DLItem* it = &dl_items[dl_count++];
            it->type = DL_PIE;
            it->page = ctx->current_page;
            it->x = cx - r;
            it->y = cy_center - r;
            it->w = r * 2;
            it->h = r * 2;
            it->c = col;
            it->stroke_c = col;
            it->stroke_w = 0;
            it->text[0] = '\0';
            it->font_size = 0;
            it->is_bold = 0;
            it->is_italic = 0;
            it->start_angle = start_angle;
            it->end_angle = end_angle;
        }
        #endif

        sb_printf(CUR_SB, "%.3f %.3f %.3f rg\n", col.r/255.0, col.g/255.0, col.b/255.0);
        sb_printf(CUR_SB, "1 1 1 RG 1.5 w\n"); 
        sb_printf(CUR_SB, "%.2f %.2f m\n", cx, cy_center);
        sb_printf(CUR_SB, "%.2f %.2f l\n", cx + r * cos(start_angle), cy_center - r * sin(start_angle));

        int segments = (int)ceil(slice_angle / (M_PI / 2.0));
        if (segments == 0) segments = 1;
        double seg_angle = slice_angle / segments;

        double cur_ang = start_angle;
        for (int s = 0; s < segments; s++) {
            double a0 = cur_ang;
            double a1 = cur_ang + seg_angle;
            double a = (a1 - a0) / 2.0;
            double kappa = (4.0 / 3.0) * (1.0 - cos(a)) / sin(a);

            double x1 = cx + r * cos(a1);
            double y1 = cy_center - r * sin(a1);

            double cp1x = (cx + r * cos(a0)) + kappa * (-r * sin(a0));
            double cp1y = (cy_center - r * sin(a0)) + kappa * (-r * cos(a0));
            double cp2x = x1 - kappa * (-r * sin(a1));
            double cp2y = y1 - kappa * (-r * cos(a1));

            sb_printf(CUR_SB, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", cp1x, cp1y, cp2x, cp2y, x1, y1);
            cur_ang = a1;
        }

        sb_printf(CUR_SB, "h B\n"); 

        // Legend
        double legend_y = (*y - 60.0) - (i * 25.0);
        double legend_x = current_x + 270.0;

        #ifdef _WIN32
        if (dl_count < 32768) {
            DLItem* it = &dl_items[dl_count++];
            it->type = DL_RECT;
            it->page = ctx->current_page;
            it->x = legend_x; it->y = legend_y - 11.0;
            it->w = 14; it->h = 14;
            it->c = col; it->stroke_w = 0;
        }
        #endif
        
        sb_printf(CUR_SB, "%.3f %.3f %.3f rg\n", col.r/255.0, col.g/255.0, col.b/255.0);
        sb_printf(CUR_SB, "%.2f %.2f 14 14 re f\n", legend_x, legend_y - 11.0);

        char legend_str[128];
        double pct = (n->chart_scores[i] / total) * 100.0;
        snprintf(legend_str, sizeof(legend_str), "%s (%g - %.1f%%)", n->chart_labels[i], n->chart_scores[i], pct);
        
        Color text_col = {51, 51, 51, "dark_gray"};
        render_styled_text(CUR_SB, legend_str, legend_x + 25.0, legend_y, 200, 11.0, 0, text_col, 0);

        start_angle = end_angle;
    }

    *y -= ch;
    break;
}
        case NODE_LINE_CHART: {
            double top_y = *y;
            if (n->chart_title[0]) {
                render_styled_text(&ctx->pages[ctx->current_page], n->chart_title, start_x, top_y, max_w, 12.0, 1, (Color){0,0,0,"black"}, 0);
                top_y -= 25.0;
            }

            double plot_top = top_y - 10.0;
            double plot_bottom = top_y - 140.0;
            double plot_height = plot_top - plot_bottom;

            double x_start_plot = start_x + (n->chart_y_label[0] ? 60.0 : 45.0);
            double x_end_plot = start_x + max_w - 20.0;
            double plot_width = x_end_plot - x_start_plot;

            double raw_max = 0;
            for (int i = 0; i < n->chart_count; i++) {
                if (n->chart_scores[i] > raw_max) raw_max = n->chart_scores[i];
            }
            double max_val = (raw_max == 0) ? 10.0 : MAX(10.0, ceil(raw_max / 10.0) * 10.0);

            for (int i = 0; i <= 4; i++) {
                double val = (max_val / 4.0) * (4 - i);
                double gy = plot_top - (i * (plot_height / 4.0));
                draw_pdf_line(ctx, x_start_plot, gy, x_end_plot, gy, (Color){229, 231, 235, "grid"}, 0.5);

                char val_str[32];
                snprintf(val_str, sizeof(val_str), "%.0f", val);
                pdf_draw_text_run(&ctx->pages[ctx->current_page], val_str, x_start_plot - 22, gy - 3, 8.0, 0, 0, (Color){102,102,102,"gray"});
            }

            draw_pdf_line(ctx, x_start_plot, plot_bottom, x_end_plot, plot_bottom, (Color){51, 51, 51, "axis"}, 1.5);
            draw_pdf_line(ctx, x_start_plot, plot_top, x_start_plot, plot_bottom, (Color){51, 51, 51, "axis"}, 1.5);

            if (n->chart_y_label[0]) {
                pdf_draw_text_run(&ctx->pages[ctx->current_page], n->chart_y_label, start_x + 5.0, plot_bottom + (plot_height / 2.0), 9.0, 0, 0, (Color){51, 51, 51, "label"});
            }


            if (n->chart_x_label[0]) {
                pdf_draw_text_run(&ctx->pages[ctx->current_page], n->chart_x_label, x_start_plot + (plot_width / 2.0) - 20.0, plot_bottom - 32.0, 9.0, 0, 0, (Color){51, 51, 51, "label"});
            }

            int num_items = n->chart_count;
            if (num_items > 0) {
                double slot_width = plot_width / (double)num_items;
                Color line_c = get_color(n->chart_color);

                double pt_x[16], pt_y[16];
                for (int i = 0; i < num_items; i++) {
                    pt_x[i] = x_start_plot + (i * slot_width) + (slot_width / 2.0);
                    double h = (n->chart_scores[i] / max_val) * plot_height;
                    pt_y[i] = plot_bottom + h;
                }

                for (int i = 0; i < num_items - 1; i++) {
                    draw_pdf_line(ctx, pt_x[i], pt_y[i], pt_x[i+1], pt_y[i+1], line_c, 2.0);
                }

                for (int i = 0; i < num_items; i++) {
                    draw_pdf_rect(ctx, pt_x[i] - 2.5, pt_y[i] + 2.5, 5.0, 5.0, line_c, 0, line_c, 0);

                    char val_str[32];
                    snprintf(val_str, sizeof(val_str), "%.0f", n->chart_scores[i]);
                    pdf_draw_text_run(&ctx->pages[ctx->current_page], val_str, pt_x[i] - 6.0, pt_y[i] + 6.0, 8.0, 0, 0, line_c);

                    pdf_draw_text_run(&ctx->pages[ctx->current_page], n->chart_labels[i], pt_x[i] - 12.0, plot_bottom - 15.0, 8.0, 0, 0, (Color){102,102,102,"gray"});
                }

                if (n->chart_trend_line && num_items > 1) {
                    double sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
                    for (int i = 0; i < num_items; i++) {
                        sum_x += i;
                        sum_y += n->chart_scores[i];
                        sum_xx += i * i;
                        sum_xy += i * n->chart_scores[i];
                    }
                    double denom = (num_items * sum_xx) - (sum_x * sum_x);
                    if (denom != 0) {
                        double m = (num_items * sum_xy - sum_x * sum_y) / denom;
                        double b = (sum_y - m * sum_x) / (double)num_items;

                        double y1_val = b;
                        double y1_px = plot_bottom + (y1_val / max_val) * plot_height;

                        double y2_val = m * (num_items - 1) + b;
                        double y2_px = plot_bottom + (y2_val / max_val) * plot_height;

                        draw_pdf_line(ctx, pt_x[0], y1_px, pt_x[num_items - 1], y2_px, (Color){220, 38, 38, "red"}, 1.5);
                    }
                }
            }

            *y = plot_bottom - (n->chart_x_label[0] ? 45.0 : 25.0);
            break;
        }

        case NODE_TSCORE_CHART: {
            double h = 105.0;
            double top_y = *y;
            draw_pdf_rect(ctx, current_x, top_y, render_w, h, get_color("#FCFCFC"), 1, get_color("#CBD5E0"), 0.5);

            char title_str[256];
            snprintf(title_str, sizeof(title_str), "#text(size: 8.5pt, fill: col-primary)[*%s*]", n->chart_title);
            char sanitized_title[256];
            sanitize_utf8_to_winansi(sanitized_title, title_str, sizeof(sanitized_title));
            render_styled_text(CUR_SB, sanitized_title, current_x + 8.0, top_y - 12.0, render_w - 16.0, 8.5, 0, get_color("#1A365D"), 0);

            double px = current_x + 10.0;
            double py_top = top_y - 24.0;
            double ph = 64.0;
            double pw = render_w - 20.0;

            draw_pdf_rect(ctx, px, py_top, pw, ph * 0.375, get_color("#F0FFF4"), 0, (Color){0}, 0);
            draw_pdf_rect(ctx, px, py_top - ph * 0.375, pw, ph * 0.375, get_color("#FFFAF0"), 0, (Color){0}, 0);
            draw_pdf_rect(ctx, px, py_top - ph * 0.75, pw, ph * 0.25, get_color("#FFF5F5"), 0, (Color){0}, 0);

            draw_pdf_line(ctx, px, py_top - ph * 0.375, px + pw, py_top - ph * 0.375, get_color("#38A169"), 0.5);
            draw_pdf_line(ctx, px, py_top - ph * 0.75, px + pw, py_top - ph * 0.75, get_color("#E53E3E"), 0.5);

            render_styled_text(CUR_SB, "#text(size: 5.5pt, fill: \"#276749\")[*Normal (>= -1.0)*]", px, py_top - 4.0, pw - 4.0, 5.5, 2, get_color("#276749"), 0);
            render_styled_text(CUR_SB, "#text(size: 5.5pt, fill: \"#C05621\")[*Osteopenia (-1.0 to -2.5)*]", px, py_top - ph * 0.375 - 4.0, pw - 4.0, 5.5, 2, get_color("#C05621"), 0);
            render_styled_text(CUR_SB, "#text(size: 5.5pt, fill: \"#9B2C2C\")[*Osteoporosis (<= -2.5)*]", px, py_top - ph * 0.75 - 4.0, pw - 4.0, 5.5, 2, get_color("#9B2C2C"), 0);

            int cnt = n->chart_count > 0 ? n->chart_count : 5;
            double col_w = pw / cnt;
            double bar_w = MIN(16.0, col_w * 0.6);

            for (int i = 0; i < cnt; i++) {
                double bar_x = px + (i + 0.5) * col_w - bar_w / 2.0;
                double val = n->chart_scores[i];
                double zero_y = py_top - ph * 0.375;
                
                double clamped_val = val;
                if (clamped_val > 1.0) clamped_val = 1.0;
                if (clamped_val < -3.0) clamped_val = -3.0;

                double score_y = py_top - ph * ((1.0 - clamped_val) / 4.0);
                double top_bar_y = (score_y > zero_y) ? score_y : zero_y;
                double bar_h = fabs(score_y - zero_y);
                if (bar_h < 1.0) bar_h = 1.0;

                Color bar_color = (val <= -2.5) ? get_color("#E53E3E") : (val <= -1.0) ? get_color("#DD6B20") : get_color("#38A169");
                draw_pdf_rect(ctx, bar_x, top_bar_y, bar_w, bar_h, bar_color, 0, (Color){0}, 0);

                char score_str[64];
                snprintf(score_str, sizeof(score_str), "#text(size: 6.5pt)[*%.1f*]", val);
                double score_text_y = (val >= 0) ? top_bar_y + 8.0 : top_bar_y - bar_h - 2.0;
                if (score_text_y > py_top - 2.0) score_text_y = py_top - 2.0;
                if (score_text_y < py_top - ph + 10.0) score_text_y = py_top - ph + 10.0;
                
                render_styled_text(CUR_SB, score_str, px + i * col_w, score_text_y, col_w, 6.5, 1, get_color("#1A365D"), 0);

                char label_str[64];
                snprintf(label_str, sizeof(label_str), "#text(size: 6.5pt)[*%s*]", n->chart_labels[i]);
                char sanitized_lbl[64];
                sanitize_utf8_to_winansi(sanitized_lbl, label_str, sizeof(sanitized_lbl));
                render_styled_text(CUR_SB, sanitized_lbl, px + i * col_w, py_top - ph - 2.0, col_w, 6.5, 1, get_color("#2D3748"), 0);
            }
            *y -= h;
            break;
        }
        case NODE_BMD_CHART: {
            double h = 105.0;
            double top_y = *y;
            draw_pdf_rect(ctx, current_x, top_y, render_w, h, get_color("#FCFCFC"), 1, get_color("#CBD5E0"), 0.5);

            char title_str[256];
            snprintf(title_str, sizeof(title_str), "#text(size: 8.5pt, fill: col-primary)[*%s*]", n->chart_title);
            char sanitized_title[256];
            sanitize_utf8_to_winansi(sanitized_title, title_str, sizeof(sanitized_title));
            render_styled_text(CUR_SB, sanitized_title, current_x + 8.0, top_y - 12.0, render_w - 16.0, 8.5, 0, get_color("#1A365D"), 0);

            double px = current_x + 10.0;
            double py_top = top_y - 24.0;
            double ph = 64.0;
            double pw = render_w - 20.0;

            draw_pdf_rect(ctx, px, py_top, pw, ph * 0.375, get_color("#F0FFF4"), 0, (Color){0}, 0);
            draw_pdf_rect(ctx, px, py_top - ph * 0.375, pw, ph * 0.375, get_color("#FFFAF0"), 0, (Color){0}, 0);
            draw_pdf_rect(ctx, px, py_top - ph * 0.75, pw, ph * 0.25, get_color("#FFF5F5"), 0, (Color){0}, 0);

            draw_pdf_line(ctx, px, py_top - ph * 0.375, px + pw, py_top - ph * 0.375, get_color("#38A169"), 0.5);
            draw_pdf_line(ctx, px, py_top - ph * 0.75, px + pw, py_top - ph * 0.75, get_color("#E53E3E"), 0.5);

            render_styled_text(CUR_SB, "#text(size: 5.5pt, fill: \"#276749\")[*Normal (>= -1.0)*]", px, py_top - 4.0, pw - 4.0, 5.5, 2, get_color("#276749"), 0);
            render_styled_text(CUR_SB, "#text(size: 5.5pt, fill: \"#C05621\")[*Osteopenia (-1.0 to -2.5)*]", px, py_top - ph * 0.375 - 4.0, pw - 4.0, 5.5, 2, get_color("#C05621"), 0);
            render_styled_text(CUR_SB, "#text(size: 5.5pt, fill: \"#9B2C2C\")[*Osteoporosis (<= -2.5)*]", px, py_top - ph * 0.75 - 4.0, pw - 4.0, 5.5, 2, get_color("#9B2C2C"), 0);

            int cnt = n->chart_count > 0 ? n->chart_count : 5;
            double col_w = pw / cnt;
            double bar_w = MIN(16.0, col_w * 0.6);

            for (int i = 0; i < cnt; i++) {
                double bar_x = px + (i + 0.5) * col_w - bar_w / 2.0;
                double val = n->chart_scores[i];
                double zero_y = py_top - ph * 0.375;
                
                double clamped_val = val;
                if (clamped_val > 1.0) clamped_val = 1.0;
                if (clamped_val < -3.0) clamped_val = -3.0;

                double score_y = py_top - ph * ((1.0 - clamped_val) / 4.0);
                double top_bar_y = (score_y > zero_y) ? score_y : zero_y;
                double bar_h = fabs(score_y - zero_y);
                if (bar_h < 1.0) bar_h = 1.0;

                Color bar_color = (val <= -2.5) ? get_color("#E53E3E") : (val <= -1.0) ? get_color("#DD6B20") : get_color("#38A169");
                draw_pdf_rect(ctx, bar_x, top_bar_y, bar_w, bar_h, bar_color, 0, (Color){0}, 0);

                char score_str[64];
                snprintf(score_str, sizeof(score_str), "#text(size: 6.5pt)[*%.1f*]", val);
                double score_text_y = (val >= 0) ? top_bar_y + 8.0 : top_bar_y - bar_h - 2.0;
                if (score_text_y > py_top - 2.0) score_text_y = py_top - 2.0;
                if (score_text_y < py_top - ph + 10.0) score_text_y = py_top - ph + 10.0;
                
                render_styled_text(CUR_SB, score_str, px + i * col_w, score_text_y, col_w, 6.5, 1, get_color("#1A365D"), 0);

                char label_str[64];
                snprintf(label_str, sizeof(label_str), "#text(size: 6.5pt)[*%s*]", n->chart_labels[i]);
                char sanitized_lbl[64];
                sanitize_utf8_to_winansi(sanitized_lbl, label_str, sizeof(sanitized_lbl));
                render_styled_text(CUR_SB, sanitized_lbl, px + i * col_w, py_top - ph - 2.0, col_w, 6.5, 1, get_color("#2D3748"), 0);
            }
            *y -= h;
            break;
        }
    }
    #undef CUR_SB
}

static void render_page_header(PdfContext* ctx, int page_num, int total_pages) {
    if (!header_script[0]) return;

    char script_buf[MAX_STR_LEN];
    char page_str[16];
    snprintf(page_str, sizeof(page_str), "%d", page_num);

    char* src = header_script;
    char* dst = script_buf;
    size_t rem = sizeof(script_buf) - 1;

    // First: Replace #counter(page).display() with page number
    while (*src && rem > 0) {
        if (strncmp(src, "#counter(page).display()", 24) == 0) {
            size_t len = strlen(page_str);
            if (len <= rem) {
                memcpy(dst, page_str, len);
                dst += len;
                rem -= len;
            }
            src += 24;
        } else if (strncmp(src, "counter(page).display()", 23) == 0) {
            size_t len = strlen(page_str);
            if (len <= rem) {
                memcpy(dst, page_str, len);
                dst += len;
                rem -= len;
            }
            src += 23;
        } else {
            *dst++ = *src++;
            rem--;
        }
    }
    *dst = '\0';

    // Second: Also replace %d with page number (for C-style format strings)
    char script_buf2[MAX_STR_LEN];
    char* src2 = script_buf;
    char* dst2 = script_buf2;
    size_t rem2 = sizeof(script_buf2) - 1;
    
    while (*src2 && rem2 > 0) {
        if (*src2 == '%' && *(src2+1) == 'd') {
            size_t len = strlen(page_str);
            if (len <= rem2) {
                memcpy(dst2, page_str, len);
                dst2 += len;
                rem2 -= len;
            }
            src2 += 2;
        } else {
            *dst2++ = *src2++;
            rem2--;
        }
    }
    *dst2 = '\0';

    int saved_page = ctx->current_page;
    int saved_disable = ctx->disable_pagebreaks;
    TextState saved_state = current_state;

    // Set the correct page for BOTH PDF and GDI display list
    ctx->current_page = page_num - 1;
    #ifdef _WIN32
    current_render_page = page_num - 1;
    #endif
    ctx->disable_pagebreaks = 1;

    Node* header_node = parse_typst_string(script_buf2);
    if (header_node) {
        // Calculate Y position for the header instead of the footer
        double header_y = page_height - (margin_top / 2.0); 
        double max_w = page_width - margin_left - margin_right;
        render_node(ctx, header_node, &header_y, max_w, margin_left);
    }

    ctx->current_page = saved_page;
    #ifdef _WIN32
    current_render_page = saved_page;
    #endif
    ctx->disable_pagebreaks = saved_disable;
    current_state = saved_state;
}

static void render_page_footer(PdfContext* ctx, int page_num, int total_pages) {
    if (!footer_script[0]) return;

    char script_buf[MAX_STR_LEN];
    char page_str[16];
    snprintf(page_str, sizeof(page_str), "%d", page_num);

    char* src = footer_script;
    char* dst = script_buf;
    size_t rem = sizeof(script_buf) - 1;

    // First: Replace #counter(page).display() with page number
    while (*src && rem > 0) {
        if (strncmp(src, "#counter(page).display()", 24) == 0) {
            size_t len = strlen(page_str);
            if (len <= rem) {
                memcpy(dst, page_str, len);
                dst += len;
                rem -= len;
            }
            src += 24;
        } else if (strncmp(src, "counter(page).display()", 23) == 0) {
            size_t len = strlen(page_str);
            if (len <= rem) {
                memcpy(dst, page_str, len);
                dst += len;
                rem -= len;
            }
            src += 23;
        } else {
            *dst++ = *src++;
            rem--;
        }
    }
    *dst = '\0';

    // Second: Also replace %d with page number (for C-style format strings)
    char script_buf2[MAX_STR_LEN];
    char* src2 = script_buf;
    char* dst2 = script_buf2;
    size_t rem2 = sizeof(script_buf2) - 1;
    
    while (*src2 && rem2 > 0) {
        if (*src2 == '%' && *(src2+1) == 'd') {
            size_t len = strlen(page_str);
            if (len <= rem2) {
                memcpy(dst2, page_str, len);
                dst2 += len;
                rem2 -= len;
            }
            src2 += 2;
        } else {
            *dst2++ = *src2++;
            rem2--;
        }
    }
    *dst2 = '\0';

    int saved_page = ctx->current_page;
    int saved_disable = ctx->disable_pagebreaks;
    TextState saved_state = current_state;

    // Set the correct page for BOTH PDF and GDI display list
    ctx->current_page = page_num - 1;
    #ifdef _WIN32
    current_render_page = page_num - 1;
    #endif
    ctx->disable_pagebreaks = 1;

    Node* footer_node = parse_typst_string(script_buf2);
    if (footer_node) {
        double footer_y = margin_bottom;
        double max_w = page_width - margin_left - margin_right;
        render_node(ctx, footer_node, &footer_y, max_w, margin_left);
    }

    ctx->current_page = saved_page;
    #ifdef _WIN32
    current_render_page = saved_page;
    #endif
    ctx->disable_pagebreaks = saved_disable;
    current_state = saved_state;
}

int export_pdf(Node* root, const char* filename) {
    FILE* f = fopen(filename, "wb");
    if (!f) return 0;

    long obj_offsets[8000]; 
    int obj_count = 0;

    fprintf(f, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");
    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", obj_count);
    
    PdfContext ctx;
    ctx.current_page = 0;
    ctx.disable_pagebreaks = 0;
    
    #ifdef _WIN32
    current_render_page = 0;
    max_page_num = 0;
    current_view_page = 0;
    dl_count = 0;
    #endif

    sb_init(&ctx.pages[0]);
    
    double cur_y = page_height - margin_top;
    double max_w = page_width - margin_left - margin_right;

    for (int i = 0; i < root->child_count; i++) {
        render_node(&ctx, root->children[i], &cur_y, max_w, margin_left);
    }
    
    int num_pages = ctx.current_page + 1;
    ctx.total_pages = num_pages;

    for (int p = 0; p < num_pages; p++) {
        render_page_header(&ctx, p + 1, num_pages);
        render_page_footer(&ctx, p + 1, num_pages);
    }

    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Pages /Kids [", obj_count);
    for (int i = 0; i < num_pages; i++) {
        fprintf(f, "%d 0 R ", 7 + (i * 2)); 
    }
    fprintf(f, "] /Count %d >>\nendobj\n", num_pages);

    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n", obj_count);
    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>\nendobj\n", obj_count);
    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-BoldOblique >>\nendobj\n", obj_count);
    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique >>\nendobj\n", obj_count);

    for (int i = 0; i < num_pages; i++) {
        obj_offsets[++obj_count] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R >>\nendobj\n",
                obj_count, page_width, page_height, obj_count + 1);
        
        obj_offsets[++obj_count] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %zu >>\nstream\n%sendstream\nendobj\n",
                obj_count, ctx.pages[i].len, ctx.pages[i].data);
        sb_free(&ctx.pages[i]);
    }

    long xref_pos = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", obj_count + 1);
    for (int i = 1; i <= obj_count; i++) {
        fprintf(f, "%010ld 00000 n \n", obj_offsets[i]);
    }
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%EOF\n", obj_count + 1, xref_pos);
    fclose(f);
    return 1;
}

static void make_output_filename(const char* input, char* output, size_t max_len) {
    strncpy(output, input, max_len - 1);
    output[max_len - 1] = '\0';
    char* dot = strrchr(output, '.');
    char* slash1 = strrchr(output, '/');
    char* slash2 = strrchr(output, '\\');
    char* last_slash = slash1 > slash2 ? slash1 : slash2;
    if (dot && dot > last_slash) *dot = '\0';
    strncat(output, ".pdf", max_len - strlen(output) - 1);
}

/* =========================================
   WIN32 GDI RENDERING WINDOW
   ========================================= */

#ifdef _WIN32
static void update_window_title(HWND hwnd) {
    char title[256];
    if (max_page_num > 0) {
        snprintf(title, sizeof(title), "Typst Document Viewer - Page %d of %d  [Click or Left/Right Arrow to switch]", current_view_page + 1, max_page_num + 1);
    } else {
        snprintf(title, sizeof(title), "Typst Document Viewer - Page 1 of 1");
    }
    SetWindowTextA(hwnd, title);
}

#ifdef _WIN32
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            update_window_title(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1; 
        case WM_LBUTTONDOWN:
            if (max_page_num > 0) {
                current_view_page = (current_view_page + 1) % (max_page_num + 1);
                update_window_title(hwnd);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            SetFocus(hwnd);
            return 0;
        case WM_RBUTTONDOWN:
            if (max_page_num > 0) {
                current_view_page = (current_view_page - 1 + max_page_num + 1) % (max_page_num + 1);
                update_window_title(hwnd);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            SetFocus(hwnd);
            return 0;
        case WM_KEYDOWN:
            if (max_page_num > 0) {
                if (wParam == VK_RIGHT || wParam == VK_DOWN || wParam == VK_NEXT || wParam == VK_SPACE) {
                    current_view_page = (current_view_page + 1) % (max_page_num + 1);
                    update_window_title(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);
                } else if (wParam == VK_LEFT || wParam == VK_UP || wParam == VK_PRIOR) {
                    current_view_page = (current_view_page - 1 + max_page_num + 1) % (max_page_num + 1);
                    update_window_title(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);
                } else if (wParam == VK_HOME) {
                    current_view_page = 0;
                    update_window_title(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);
                } else if (wParam == VK_END) {
                    current_view_page = max_page_num;
                    update_window_title(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            int win_w = rc.right - rc.left;
            int win_h = rc.bottom - rc.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, win_w, win_h);
            HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

            double sx = (double)(win_w - 40) / page_width;
            double sy = (double)(win_h - 40) / page_height;
            double scale = sx < sy ? sx : sy; 
            if (scale <= 0) scale = 0.1;

            int pw = (int)(page_width * scale);
            int ph = (int)(page_height * scale);
            int offset_x = (win_w - pw) / 2;
            int offset_y = (win_h - ph) / 2;
            
            HBRUSH winBg = CreateSolidBrush(RGB(230, 230, 230));
            FillRect(memDC, &rc, winBg);
            DeleteObject(winBg);

            HBRUSH pageBg = CreateSolidBrush(RGB(255, 255, 255));
            HPEN pagePen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
            HGDIOBJ obPage = SelectObject(memDC, pageBg);
            HGDIOBJ opPage = SelectObject(memDC, pagePen);
            Rectangle(memDC, offset_x, offset_y, offset_x + pw, offset_y + ph);
            SelectObject(memDC, obPage);
            SelectObject(memDC, opPage);
            DeleteObject(pageBg);
            DeleteObject(pagePen);

            SetBkMode(memDC, TRANSPARENT);

            for (int i = 0; i < dl_count; i++) {
                DLItem* it = &dl_items[i];
                if (it->page != current_view_page) continue;
                
                int map_x = offset_x + (int)(it->x * scale);
                int map_w = (int)(it->w * scale);
                
                if (it->type == DL_RECT) {
                    int map_y = offset_y + (int)((page_height - (it->y + it->h)) * scale);
                    int map_h = (int)(it->h * scale);
                    
                    HBRUSH br = CreateSolidBrush(RGB(it->c.r, it->c.g, it->c.b));
                    HPEN pen = (it->stroke_w > 0) ? CreatePen(PS_SOLID, max(1, (int)(it->stroke_w * scale)), RGB(it->stroke_c.r, it->stroke_c.g, it->stroke_c.b)) : CreatePen(PS_NULL, 0, 0);
                    
                    HGDIOBJ ob = SelectObject(memDC, br);
                    HGDIOBJ op = SelectObject(memDC, pen);
                    
                    Rectangle(memDC, map_x, map_y, map_x + map_w, map_y + map_h);
                    
                    SelectObject(memDC, ob);
                    SelectObject(memDC, op);
                    DeleteObject(br);
                    DeleteObject(pen);
                }
                else if (it->type == DL_LINE) {
                    int map_y = offset_y + (int)((page_height - it->y) * scale);
                    int map_y2 = offset_y + (int)((page_height - (it->y + it->h)) * scale);
                    
                    HPEN pen = CreatePen(PS_SOLID, max(1, (int)(it->stroke_w * scale)), RGB(it->c.r, it->c.g, it->c.b));
                    HGDIOBJ op = SelectObject(memDC, pen);
                    MoveToEx(memDC, map_x, map_y, NULL);
                    LineTo(memDC, map_x + map_w, map_y2);
                    SelectObject(memDC, op);
                    DeleteObject(pen);
                }
                else if (it->type == DL_TEXT) {
                    int map_y = offset_y + (int)((page_height - it->y) * scale);
                    SetTextAlign(memDC, TA_LEFT | TA_BASELINE);
                    SetTextColor(memDC, RGB(it->c.r, it->c.g, it->c.b));
                    
                    int font_height = (int)(it->font_size * scale);
                    if (font_height < 1) font_height = 1;
                    
                    int font_weight = it->is_bold ? FW_BOLD : FW_NORMAL;
                    DWORD italic = it->is_italic ? TRUE : FALSE;
                    
                    HFONT font = CreateFontA(
                        -font_height, 0, 0, 0, font_weight, italic, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial"
                    );
                    
                    HGDIOBJ of = SelectObject(memDC, font);
                    TextOutA(memDC, map_x, map_y, it->text, (int)strlen(it->text));
                    SelectObject(memDC, of);
                    DeleteObject(font);
                }
                else if (it->type == DL_LINE) {
                    int map_y = offset_y + (int)((page_height - it->y) * scale);
                    int map_y2 = offset_y + (int)((page_height - (it->y + it->h)) * scale);
                    
                    HPEN pen = CreatePen(PS_SOLID, max(1, (int)(it->stroke_w * scale)), RGB(it->c.r, it->c.g, it->c.b));
                    HGDIOBJ op = SelectObject(memDC, pen);
                    MoveToEx(memDC, map_x, map_y, NULL);
                    LineTo(memDC, map_x + map_w, map_y2);
                    SelectObject(memDC, op);
                    DeleteObject(pen);
                }
                else if (it->type == DL_TEXT) {
                    int map_y = offset_y + (int)((page_height - it->y) * scale);
                    SetTextAlign(memDC, TA_LEFT | TA_BASELINE);
                    SetTextColor(memDC, RGB(it->c.r, it->c.g, it->c.b));
                    
                    int font_height = (int)(it->font_size * scale);
                    if (font_height < 1) font_height = 1;
                    
                    int font_weight = it->is_bold ? FW_BOLD : FW_NORMAL;
                    DWORD italic = it->is_italic ? TRUE : FALSE;
                    
                    HFONT font = CreateFontA(
                        -font_height, 0, 0, 0, font_weight, italic, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial"
                    );
                    
                    HGDIOBJ of = SelectObject(memDC, font);
                    TextOutA(memDC, map_x, map_y, it->text, (int)strlen(it->text));
                    SelectObject(memDC, of);
                    DeleteObject(font);
                }
        else if (it->type == DL_PIE) {
            int map_y = offset_y + (int)((page_height - (it->y + it->h)) * scale);
            int map_h = (int)(it->h * scale);
            
            int center_x = map_x + map_w / 2;
            int center_y = map_y + map_h / 2;
            int radius = map_w / 2;
            if (radius < 1) radius = 1;
            
            int num_pts = 32;
            POINT pts[34];
            pts[0].x = center_x;
            pts[0].y = center_y;
            
            for (int k = 0; k <= num_pts; k++) {
                double a = it->start_angle + (it->end_angle - it->start_angle) * (double)k / (double)num_pts;
                pts[k + 1].x = center_x + (int)(radius * cos(a));
                pts[k + 1].y = center_y - (int)(radius * sin(a));
            }
            
            HBRUSH br = CreateSolidBrush(RGB(it->c.r, it->c.g, it->c.b));
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            HGDIOBJ ob = SelectObject(memDC, br);
            HGDIOBJ op = SelectObject(memDC, pen);
            Polygon(memDC, pts, num_pts + 2);
            SelectObject(memDC, ob);
            SelectObject(memDC, op);
            DeleteObject(br);
            DeleteObject(pen);
        }

            }


            BitBlt(hdc, 0, 0, win_w, win_h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif

void show_gdi_window() {
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = "TypstGDIClass";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, "TypstGDIClass", "Typst Document Viewer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 800,
        NULL, NULL, wc.hInstance, NULL);

    SetFocus(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
#endif

/* =========================================
   ENTRY POINT
   ========================================= */

int main(int argc, char** argv) {
    init_colors();
    current_state.font_size = 10.0;
    current_state.fill_color = get_color("#2D3748");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.typ> [output.pdf]\n", argv[0]);
        return 1;
    }
    printf("[1/3] Reading input file '%s'...\n", argv[1]);
    FILE* fp = fopen(argv[1], "r");
    if (!fp) return 1;
    size_t n = fread(source, 1, sizeof(source) - 1, fp);
    source[n] = '\0';
    fclose(fp);

    printf("[2/3] Parsing Typst document nodes...\n");
    Node* doc = parse_document();
    if (!doc) return 1;

    char out_pdf[512];
    if (argc >= 3) {
        strncpy(out_pdf, argv[2], sizeof(out_pdf) - 1);
        out_pdf[sizeof(out_pdf) - 1] = '\0';
    } else {
        make_output_filename(argv[1], out_pdf, sizeof(out_pdf));
    }

    printf("[3/3] Generating PDF output '%s'...\n", out_pdf);
    if (!export_pdf(doc, out_pdf)) return 1;

    printf("Success! Document compiled to '%s' (%d top-level nodes rendered).\n", out_pdf, doc->child_count);
    
#ifdef _WIN32
    printf("[4/3] Opening interactive GDI preview window...\n");
    show_gdi_window();
#endif

    return 0;
}