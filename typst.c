/* ============================================================================
 *
 * typst clone
 * Typst-like C Rendering Engine 
 * Compile: gcc -Os -s -o typst.exe typst.c -lm
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 *
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>

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

/* =========================================
   TYPE DEFINITIONS
   ========================================= */

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
    NODE_VSPACE, NODE_HSPACE, NODE_PAGE, NODE_HEADER, NODE_FOOTER,
    NODE_PAGEBREAK
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

    Node* children[512];
    int child_count;
};

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StreamBuffer;

/* =========================================
   GLOBAL STATE
   ========================================= */

static Color colors[MAX_COLORS];
static int color_count = 0;
static TextState current_state;

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
static char footer_script[MAX_STR_LEN] = {0};

/* =========================================
   FORWARD DECLARATIONS
   ========================================= */

static Node* parse_element(void);
static void render_node(StreamBuffer* sb, Node* n, double* y, double max_w, double start_x, int page_idx);
static double measure_node_height(Node* n, double max_w);

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
    sscanf(hex + 1, "%02x%02x%02x", &c->r, &c->g, &c->b);
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
        if (strcmp(colors[i].name, spec) == 0) return colors[i];
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

static int extract_param_value(const char* params, const char* key, char* value, int max_len) {
    const char* p = strstr(params, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '=') p++;
    int i = 0, depth_p = 0, depth_b = 0, depth_bk = 0;
    while (*p && i < max_len - 1) {
        if (*p == '(') depth_p++;
        else if (*p == ')') { if (depth_p == 0) break; depth_p--; }
        else if (*p == '{') depth_b++;
        else if (*p == '}') { if (depth_b == 0) break; depth_b--; }
        else if (*p == '[') depth_bk++;
        else if (*p == ']') { if (depth_bk == 0) break; depth_bk--; }
        else if (*p == ',' && depth_p == 0 && depth_b == 0 && depth_bk == 0) break;
        value[i++] = *p++;
    }
    while (i > 0 && isspace((unsigned char)value[i-1])) i--;
    value[i] = '\0';
    if (value[0] == '"' || value[0] == '\'') {
        memmove(value, value + 1, i); i--;
        if (i > 0 && (value[i-1] == '"' || value[i-1] == '\'')) value[i-1] = '\0';
    }
    return 1;
}

/* =========================================
   TEXT RUN & INLINE STYLING PARSER
   ========================================= */

static void parse_inline_runs(const char* in, TextState base_state, TextRun runs[], int* run_count) {
    *run_count = 0;
    TextState state = base_state;
    char buf[MAX_STR_LEN] = {0};
    int bi = 0;
    const char* p = in;
    
    while (*p) {
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
                    while (start >= params && (isdigit(*start) || *start == '.')) start--;
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
        if (extract_param_value(params, "margin", val, sizeof(val))) {
            char mval[32];
            if (extract_param_value(val, "x", mval, sizeof(mval))) {
                margin_left = parse_size(mval); margin_right = parse_size(mval);
            }
            if (extract_param_value(val, "top", mval, sizeof(mval))) margin_top = parse_size(mval);
            if (extract_param_value(val, "bottom", mval, sizeof(mval))) margin_bottom = parse_size(mval);
        }
        if (extract_param_value(params, "header", header_script, sizeof(header_script))) {}
        if (extract_param_value(params, "footer", footer_script, sizeof(footer_script))) {}
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
                while (start >= params && (isdigit(*start) || *start == '.')) start--;
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
                l->has_width = 2; // 2 indicates percentage relative to container width
            } else {
                l->width = parse_size(length);
                l->has_width = 1; // 1 indicates absolute size
            }
        }
    }
    return l;
}

static Node* parse_block_node(NodeType type) {
    Node* b = alloc_node(type);
    if (!b) return NULL;
    if (type == NODE_RECT) {
        if (strncmp(&source[source_pos], "#rect", 5) == 0) match_str("#rect"); else match_str("rect");
    } else {
        if (strncmp(&source[source_pos], "#block", 6) == 0) match_str("#block"); else match_str("block");
    }
    skip_whitespace_and_comments();
    
    char param[MAX_STR_LEN] = {0}; 
    int expects_closing_paren = 0;
    
    if (source[source_pos] == '(') {
        source_pos++;
        int pi = 0, depth = 1;
        while (source[source_pos] && depth > 0) {
            if (depth == 1 && source[source_pos] == '[') {
                expects_closing_paren = 1;
                break;
            }
            if (source[source_pos] == '(') depth++; else if (source[source_pos] == ')') depth--;
            if (depth > 0 && pi < MAX_STR_LEN - 1) param[pi++] = source[source_pos];
            source_pos++;
        }
        param[pi] = '\0';
    }
    char val[MAX_STR_LEN];
    if (extract_param_value(param, "fill", val, sizeof(val))) b->fill_color = get_color(val);
    if (extract_param_value(param, "stroke", val, sizeof(val))) {
        b->has_stroke = 1; b->stroke_color = get_color(val); b->stroke_width = 0.5;
    }
    if (extract_param_value(param, "inset", val, sizeof(val))) b->inset = parse_size(val);
    if (extract_param_value(param, "width", val, sizeof(val))) {
        if (strstr(val, "%")) {
            b->width = atof(val) / 100.0;
            b->has_width = 2; // 2 indicates percentage relative to container width
        } else {
            b->width = parse_size(val);
            b->has_width = 1; // 1 indicates absolute size
        }
    }

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
    if (type == NODE_TABLE) {
        if (strncmp(&source[source_pos], "#table", 6) == 0) match_str("#table"); else match_str("table");
    } else {
        if (strncmp(&source[source_pos], "#grid", 5) == 0) match_str("#grid"); else match_str("grid");
    }

    skip_whitespace_and_comments();
    char param[MAX_STR_LEN] = {0}; 
    int pi = 0, depth = 0;
    
    if (source[source_pos] == '(') {
        depth = 1; source_pos++;
        while (source[source_pos] && depth > 0) {
            if (depth == 1 && (source[source_pos] == '[' || source[source_pos] == '"' || 
                               strncmp(&source[source_pos], "rect(", 5) == 0 ||
                               strncmp(&source[source_pos], "#rect(", 6) == 0 ||
                               strncmp(&source[source_pos], "align(", 6) == 0 ||
                               strncmp(&source[source_pos], "#align(", 7) == 0 ||
                               strncmp(&source[source_pos], "text(", 5) == 0 ||
                               strncmp(&source[source_pos], "#text(", 6) == 0)) {
                break;
            }
            if (source[source_pos] == '(' || source[source_pos] == '[') depth++;
            else if (source[source_pos] == ')' || source[source_pos] == ']') depth--;
            
            if (depth > 0 && pi < MAX_STR_LEN - 1) param[pi++] = source[source_pos];
            source_pos++;
        }
        param[pi] = '\0';
    }

    char cols_str[MAX_STR_LEN];
    if (extract_param_value(param, "columns", cols_str, sizeof(cols_str))) {
        if (cols_str[0] == '(') {
            const char* p = cols_str + 1; int col = 0;
            while (*p && *p != ')' && col < MAX_TABLE_COLS) {
                char buf[32]; int i = 0;
                while (*p && *p != ',' && *p != ')' && i < 31) {
                    if (!isspace((unsigned char)*p)) buf[i++] = *p; p++;
                }
                buf[i] = '\0';
                if (strstr(buf, "fr")) { t->is_fr[col] = 1; t->col_widths[col] = atof(buf); }
                else { t->is_fr[col] = 0; t->col_widths[col] = parse_size(buf); }
                col++; if (*p == ',') p++;
            }
            t->cell_cols = col > 0 ? col : 1;
        }
    } else { t->cell_cols = 1; t->is_fr[0] = 1; t->col_widths[0] = 1.0; }

    char align_str[MAX_STR_LEN];
    if (extract_param_value(param, "align", align_str, sizeof(align_str))) {
        if (align_str[0] == '(') {
            const char* p = align_str + 1; int col = 0;
            while (*p && *p != ')' && col < MAX_TABLE_COLS) {
                if (strstr(p, "center")) t->col_aligns[col] = 1;
                else if (strstr(p, "right")) t->col_aligns[col] = 2;
                else t->col_aligns[col] = 0;
                while (*p && *p != ',' && *p != ')') p++;
                if (*p == ',') { p++; col++; }
            }
        }
    }

    char fill_val[MAX_STR_LEN];
    if (extract_param_value(param, "fill", fill_val, sizeof(fill_val))) {
        if (strstr(fill_val, "=>")) {
            t->alt_rows = 1;
            t->header_fill = get_color("#2B6CB0");
            t->stripe_fill_1 = get_color("#FFFFFF");
            t->stripe_fill_2 = get_color("#F7FAFC");
        } else { t->fill_color = get_color(fill_val); }
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
    char val[32];
    if (extract_param_value(param, "inset", val, sizeof(val))) t->inset = parse_size(val);
    if (extract_param_value(param, "gutter", val, sizeof(val))) t->gutter = parse_size(val);

    int cell = 0;
    while (source[source_pos]) {
        skip_whitespace_and_comments();
        if (source[source_pos] == ')' || source[source_pos] == ']') { consume(); break; }

        int row = cell / t->cell_cols; int col = cell % t->cell_cols;
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
        } else if (strncmp(&source[source_pos], "rect(", 5) == 0 || strncmp(&source[source_pos], "#rect(", 6) == 0) {
            Node* child = parse_block_node(NODE_RECT);
            if (child) cell_node->children[cell_node->child_count++] = child;
        } else {
            int ci = 0;
            int depth_p = 0, depth_b = 0;
            while (source[source_pos] && ci < MAX_STR_LEN - 1) {
                char c = source[source_pos];
                
                if (depth_p == 0 && depth_b == 0 && (c == ',' || c == ')' || c == ']')) {
                    break;
                }
                
                if (c == '(') depth_p++;
                else if (c == ')') depth_p--;
                else if (c == '[') depth_b++;
                else if (c == ']') depth_b--;

                cell_node->content[ci++] = source[source_pos++];
            }
            while (ci > 0 && isspace((unsigned char)cell_node->content[ci-1])) ci--;
            cell_node->content[ci] = '\0';
        }

        if (row < MAX_GRID_ROWS && col < MAX_TABLE_COLS) t->cells[row][col] = cell_node;
        cell++;

        skip_whitespace_and_comments();
        if (source[source_pos] == ',') consume();
    }
    t->cell_rows = cell == 0 ? 1 : ((cell - 1) / t->cell_cols + 1);
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
    if (strncmp(&source[source_pos], "#show", 5) == 0 ||
        strncmp(&source[source_pos], "#let", 4) == 0 ||
        strncmp(&source[source_pos], "#import", 7) == 0 ||
        strncmp(&source[source_pos], "#pagebreak", 10) == 0) {
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

    if (strncmp(&source[source_pos], "#line", 5) == 0 || strncmp(&source[source_pos], "line(", 5) == 0) return parse_line();
    if (strncmp(&source[source_pos], "#rect", 5) == 0 || strncmp(&source[source_pos], "rect(", 5) == 0) return parse_block_node(NODE_RECT);
    if (strncmp(&source[source_pos], "#grid", 5) == 0 || strncmp(&source[source_pos], "grid(", 5) == 0) return parse_table_or_grid(NODE_GRID);
    if (strncmp(&source[source_pos], "#table", 6) == 0 || strncmp(&source[source_pos], "table(", 6) == 0) return parse_table_or_grid(NODE_TABLE);

    if (strncmp(&source[source_pos], "#align(", 7) == 0 || strncmp(&source[source_pos], "#pad(", 5) == 0) {
        Node* p = alloc_node(NODE_PARAGRAPH);
        if (!p) return NULL;
        if (strncmp(&source[source_pos], "#align(", 7) == 0) {
            if (strstr(&source[source_pos], "center")) p->align = 1;
            else if (strstr(&source[source_pos], "right")) p->align = 2;
        }
        while (source[source_pos] && source[source_pos] != ')' && source[source_pos] != '\n') source_pos++;
        if (source[source_pos] == ')') source_pos++;
        skip_whitespace_and_comments();
        if (source[source_pos] == '[') {
            extract_bracket_content(p->content, MAX_STR_LEN);
        } else {
            int i = 0;
            while (source[source_pos] && source[source_pos] != '\n' && i < MAX_STR_LEN - 1) {
                p->content[i++] = source[source_pos++];
            }
            while (i > 0 && isspace((unsigned char)p->content[i-1])) i--;
            p->content[i] = '\0';
        }
        return p;
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
    char escaped[MAX_STR_LEN];
    pdf_escape(text, escaped, sizeof(escaped));
    
    const char* font = "F1";
    if (is_bold && is_italic) font = "F3";
    else if (is_bold) font = "F2";
    else if (is_italic) font = "F4";
    
    sb_printf(sb, "BT\n/%s %.1f Tf\n%.3f %.3f %.3f rg\n%.2f %.2f Td\n(%s) Tj\nET\n0 0 0 rg\n",
              font, font_size, text_color.r / 255.0, text_color.g / 255.0, text_color.b / 255.0,
              x, y, escaped);
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
        ['y'] = 500, ['z'] = 500, ['{'] = 333, ['|'] = 260, ['}'] = 333, ['~'] = 584
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
            if (n->child_count > 0) {
                double h = 0;
                for (int i = 0; i < n->child_count; i++) h += measure_node_height(n->children[i], max_w);
                return h;
            }
            return render_styled_text(NULL, n->content, 0, 0, max_w, n->font_size, n->align, current_state.fill_color, 1);
        }
        case NODE_RECT: {
            double rw = max_w;
            if (n->has_width) {
                rw = (n->has_width == 2) ? n->width * max_w : n->width;
            }
            double content_h = 0;
            if (n->child_count > 0) {
                for (int i = 0; i < n->child_count; i++) content_h += measure_node_height(n->children[i], rw - n->inset*2);
            } else {
                content_h = render_styled_text(NULL, n->content, 0, 0, rw - n->inset*2, n->font_size, 0, current_state.fill_color, 1);
            }
            return content_h + n->inset * 2.0;
        }
        default: return 16.0;
    }
}

/* =========================================
   NODE RENDERER
   ========================================= */

static void render_node(StreamBuffer* sb, Node* n, double* y, double max_w, double start_x, int page_idx) {
    if (!n) return;
    if (*y < margin_bottom + 20.0) {
        *y = page_height - margin_top;
    }

    double render_w = n->has_width ? n->width : max_w;
    double current_x = start_x;

    switch (n->type) {
        case NODE_HEADING: {
            double h = render_styled_text(sb, n->content, current_x, *y, render_w, n->font_size, n->align, get_color("#2B6CB0"), 0);
            *y -= (h + 8.0);
            break;
        }
        case NODE_PARAGRAPH: {
            double h = render_styled_text(sb, n->content, current_x, *y, render_w, n->font_size, n->align, current_state.fill_color, 0);
            *y -= h;
            break;
        }
        case NODE_BLOCK: {
            for (int i = 0; i < n->child_count; i++) {
                render_node(sb, n->children[i], y, render_w, current_x, page_idx);
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
            sb_printf(sb, "%.3f %.3f %.3f RG %.2f w\n", lc.r/255.0, lc.g/255.0, lc.b/255.0, sw);
            sb_printf(sb, "%.2f %.2f m %.2f %.2f l S\n0 0 0 RG\n", current_x, *y, current_x + lw, *y);
            *y -= 8.0;
            break;
        }
        case NODE_VSPACE: {
            *y -= n->height;
            break;
        }
case NODE_RECT: {
            double render_w = max_w;
            if (n->has_width) {
                render_w = (n->has_width == 2) ? n->width * max_w : n->width;
            }
            double inset = n->inset > 0 ? n->inset : 8.0;
            double content_h = 0;
            if (n->child_count > 0) {
                for (int i = 0; i < n->child_count; i++) content_h += measure_node_height(n->children[i], render_w - inset*2);
            } else {
                content_h = render_styled_text(NULL, n->content, 0, 0, render_w - inset*2, n->font_size, 0, current_state.fill_color, 1);
            }
            double total_h = content_h + inset * 2.0;

            sb_printf(sb, "%.3f %.3f %.3f rg\n", n->fill_color.r/255.0, n->fill_color.g/255.0, n->fill_color.b/255.0);
            sb_printf(sb, "%.2f %.2f %.2f %.2f re f\n", current_x, *y - total_h, render_w, total_h);

            if (n->has_stroke) {
                sb_printf(sb, "%.3f %.3f %.3f RG %.2f w\n", n->stroke_color.r/255.0, n->stroke_color.g/255.0, n->stroke_color.b/255.0, n->stroke_width);
                sb_printf(sb, "%.2f %.2f %.2f %.2f re S\n0 0 0 RG\n", current_x, *y - total_h, render_w, total_h);
            }

            if (n->child_count > 0) {
                double child_y = *y - inset;
                for (int i = 0; i < n->child_count; i++) render_node(sb, n->children[i], &child_y, render_w - inset*2, current_x + inset, page_idx);
            } else {
                render_styled_text(sb, n->content, current_x + inset, *y - inset, render_w - inset*2, n->font_size, 0, current_state.fill_color, 0);
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
                            ch = render_styled_text(NULL, cell->content, 0, 0, actual_widths[c], n->font_size, cell->align, current_state.fill_color, 1);
                        }
                        max_row_h = MAX(max_row_h, ch);
                    }
                }

                for (int c = 0; c < n->cell_cols; c++) {
                    Node* cell = n->cells[r][c];
                    if (cell) {
                        if (cell->child_count > 0) {
                            double child_y = *y;
                            for (int k = 0; k < cell->child_count; k++) render_node(sb, cell->children[k], &child_y, actual_widths[c], cx, page_idx);
                        } else {
                            render_styled_text(sb, cell->content, cx, *y, actual_widths[c], n->font_size, cell->align, current_state.fill_color, 0);
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
            for (int i = 0; i < n->cell_cols; i++) {
                actual_widths[i] = n->is_fr[i] ? n->col_widths[i] * fr_unit : n->col_widths[i];
            }

            double inset = n->inset > 0 ? n->inset : 6.0;

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
                double cell_box_h = max_row_h + inset * 2.0;

                Color row_fill;
                if (r == 0 && n->alt_rows) row_fill = n->header_fill;
                else if (n->alt_rows) row_fill = (r % 2 == 0) ? n->stripe_fill_2 : n->stripe_fill_1;
                else row_fill = n->fill_color;

                double cx = current_x;
                for (int c = 0; c < n->cell_cols; c++) {
                    Node* cell = n->cells[r][c];

                    // Draw individual cell fill
                    sb_printf(sb, "%.3f %.3f %.3f rg\n", row_fill.r/255.0, row_fill.g/255.0, row_fill.b/255.0);
                    sb_printf(sb, "%.2f %.2f %.2f %.2f re f\n", cx, *y - cell_box_h, actual_widths[c], cell_box_h);

                    // Draw individual cell stroke / borders (includes vertical dividers)
                    if (n->has_stroke) {
                        sb_printf(sb, "%.3f %.3f %.3f RG %.2f w\n", n->stroke_color.r/255.0, n->stroke_color.g/255.0, n->stroke_color.b/255.0, n->stroke_width);
                        sb_printf(sb, "%.2f %.2f %.2f %.2f re S\n0 0 0 RG\n", cx, *y - cell_box_h, actual_widths[c], cell_box_h);
                    }

                    if (cell) {
                        Color tc = (r == 0 && n->alt_rows) ? (Color){255,255,255,"white"} : current_state.fill_color;
                        if (cell->child_count > 0) {
                            Color old_color = current_state.fill_color;
                            current_state.fill_color = tc;
                            double child_y = *y - inset;
                            for (int k = 0; k < cell->child_count; k++) render_node(sb, cell->children[k], &child_y, actual_widths[c] - inset*2, cx + inset, page_idx);
                            current_state.fill_color = old_color;
                        } else {
                            render_styled_text(sb, cell->content, cx + inset, *y - inset, actual_widths[c] - inset*2, n->font_size, cell->align, tc, 0);
                        }
                    }
                    cx += actual_widths[c];
                }
                *y -= cell_box_h;
            }
            break;
        }
        default: break;
    }
}

/* =========================================
   PDF EXPORT ENGINE (IN-MEMORY)
   ========================================= */

int export_pdf(Node* root, const char* filename) {
    FILE* f = fopen(filename, "wb");
    if (!f) return 0;

    long obj_offsets[100];
    int obj_count = 0;

    fprintf(f, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");
    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", obj_count);
    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Pages /Kids [7 0 R] /Count 1 >>\nendobj\n", obj_count);
    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n", obj_count);
    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>\nendobj\n", obj_count);
    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-BoldOblique >>\nendobj\n", obj_count);
    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique >>\nendobj\n", obj_count);

    StreamBuffer sb;
    sb_init(&sb);

    double cur_y = page_height - margin_top;
    double max_w = page_width - margin_left - margin_right;

    for (int i = 0; i < root->child_count; i++) {
        render_node(&sb, root->children[i], &cur_y, max_w, margin_left, 0);
    }
    if (strlen(footer_script) > 0) {
        render_styled_text(&sb, "Page 1 | Confidential Technical Field Service Documentation",
                           margin_left, margin_bottom / 2.0 + 8.0, max_w, 8.0, 1, get_color("#718096"), 0);
    }

    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] ", obj_count, page_width, page_height);
    fprintf(f, "/Contents %d 0 R /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> >>\nendobj\n", obj_count + 1);

    obj_offsets[++obj_count] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Length %zu >>\nstream\n", obj_count, sb.len);
    if (sb.len > 0 && sb.data) fwrite(sb.data, 1, sb.len, f);
    fprintf(f, "\nendstream\nendobj\n");

    sb_free(&sb);

    long xref = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", obj_count + 1);
    for (int i = 1; i <= obj_count; i++) fprintf(f, "%010ld 00000 n \n", obj_offsets[i]);
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", obj_count + 1, xref);

    fflush(f);
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
    return 0;
}