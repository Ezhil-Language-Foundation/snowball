
#include <stdlib.h> /* for exit */
#include <string.h> /* for strlen */
#include <stdio.h> /* for fprintf etc */
#include "header.h"

/* prototypes */

static void generate(struct generator * g, struct node * p);
static void w(struct generator * g, const char * s);
static void writef(struct generator * g, const char * s, struct node * p);


enum special_labels {
    x_return = -1
};

static int new_label(struct generator * g) {

    return g->next_label++;
}

static struct str * vars_newname(struct generator * g) {

    struct str * output;
    g->var_number ++;
    output = str_new();
    str_append_string(output, "v_");
    str_append_int(output, g->var_number);
    return output;
}

/* Output routines */
static void output_str(FILE * outfile, struct str * str) {

    char * s = b_to_s(str_data(str));
    fprintf(outfile, "%s", s);
    free(s);
}

/* Write routines for simple entities */

static void write_char(struct generator * g, int ch) {

    str_append_ch(g->outbuf, ch);
}

static void write_newline(struct generator * g) {

    str_append_string(g->outbuf, "\n");
}

static void write_string(struct generator * g, const char * s) {
    str_append_string(g->outbuf, s);
}

static void write_b(struct generator * g, symbol * b) {

    str_append_b(g->outbuf, b);
}

static void write_str(struct generator * g, struct str * str) {

    str_append(g->outbuf, str);
}

static void write_int(struct generator * g, int i) {

    str_append_int(g->outbuf, i);
}


/* Write routines for items from the syntax tree */

static void write_varname(struct generator * g, struct name * p) {

    int ch = "SBIrxg"[p->type];
    if (p->type != t_external)
    {
        write_char(g, ch);
        write_char(g, '_');
    }
    str_append_b(g->outbuf, p->b);
}

static void write_varref(struct generator * g, struct name * p) {

    /* In jsx, references look just the same */
    write_varname(g, p);
}

static void write_hexdigit(struct generator * g, int n) {

    write_char(g, n < 10 ? n + '0' : n - 10 + 'A');
}

static void write_hex(struct generator * g, int ch) {

    write_string(g, "\\u");
    {
        int i;
        for (i = 12; i >= 0; i -= 4) write_hexdigit(g, ch >> i & 0xf);
    }
}

static void write_literal_string(struct generator * g, symbol * p) {

    int i;
    write_string(g, "\"");
    for (i = 0; i < SIZE(p); i++) {
        int ch = p[i];
        if (32 <= ch && ch <= 127) {
            if (ch == '\"' || ch == '\\') write_string(g, "\\");
            write_char(g, ch);
        } else {
            write_hex(g, ch);
        }
    }
    write_string(g, "\"");
}

static void write_margin(struct generator * g) {

    int i;
    for (i = 0; i < g->margin; i++) write_string(g, "    ");
}

/* Write a variable declaration. */
static void write_declare(struct generator * g,
                          char * declaration,
                          struct node * p) {

    struct str * temp = g->outbuf;
    g->outbuf = g->declarations;
    write_string(g, "        var ");
    writef(g, declaration, p);
    write_string(g, ";");
    write_newline(g);
    g->outbuf = temp;
}

static void write_comment(struct generator * g, struct node * p) {

    write_margin(g);
    write_string(g, "// ");
    write_string(g, (char *) name_of_token(p->type));
    if (p->name != 0) {
        write_string(g, " ");
        str_append_b(g->outbuf, p->name->b);
    }
    write_string(g, ", line ");
    write_int(g, p->line_number);
    write_newline(g);
}

static void write_block_start(struct generator * g) {

    w(g, "~M{~+~N");
}

static void write_block_end(struct generator * g)    /* block end */ {

    w(g, "~-~M}~N");
}

static void write_savecursor(struct generator * g, struct node * p,
                             struct str * savevar) {

    g->B[0] = str_data(savevar);
    g->S[1] = "";
    if (p->mode != m_forward) g->S[1] = "this.limit - ";
    write_declare(g, "~B0 : int", p);
    writef(g, "~M~B0 = ~S1this.cursor;~N" , p);
}

static void restore_string(struct node * p, struct str * out, struct str * savevar) {

    str_clear(out);
    str_append_string(out, "this.cursor = ");
    if (p->mode != m_forward) str_append_string(out, "this.limit - ");
    str_append(out, savevar);
    str_append_string(out, ";");
}

static void write_restorecursor(struct generator * g, struct node * p,
                                struct str * savevar) {

    struct str * temp = str_new();
    write_margin(g);
    restore_string(p, temp, savevar);
    write_str(g, temp);
    write_newline(g);
    str_delete(temp);
}

static void write_inc_cursor(struct generator * g, struct node * p) {

    write_margin(g);
    write_string(g, p->mode == m_forward ? "this.cursor++;" : "this.cursor--;");
    write_newline(g);
}

static void wsetlab_begin(struct generator * g, int n) {
    g->I[0] = n;
    w(g, "~Mvar lab~I0 = true;~N");
    w(g, "~Mlab~I0: while (lab~I0 == true)~N~M{~N");
    w(g, "~+~Mlab~I0 = false;~N");
}

static void wsetlab_end(struct generator * g) {

    w(g, "~-~M}~N");
}

static void wgotol(struct generator * g, int n) {

    write_margin(g);
    write_string(g, "break lab");
    write_int(g, n);
    write_string(g, ";");
    write_newline(g);
}

static void write_failure(struct generator * g) {

    if (str_len(g->failure_str) != 0) {
        write_margin(g);
        write_str(g, g->failure_str);
        write_newline(g);
    }
    write_margin(g);
    switch (g->failure_label)
    {
        case x_return:
            write_string(g, "return false;");
            g->unreachable = true;
            break;
        default:
            write_string(g, "break lab");
            write_int(g, g->failure_label);
            write_string(g, ";");
            //g->unreachable = true;
    }
    write_newline(g);
}

static void write_failure_if(struct generator * g, char * s, struct node * p) {

    writef(g, "~Mif (", p);
    writef(g, s, p);
    writef(g, ")~N", p);
    write_block_start(g);
    write_failure(g);
    write_block_end(g);
    g->unreachable = false;
}

/* if at limit fail */
static void write_check_limit(struct generator * g, struct node * p) {

    if (p->mode == m_forward) {
        write_failure_if(g, "this.cursor >= this.limit", p);
    } else {
        write_failure_if(g, "this.cursor <= this.limit_backward", p);
    }
}

/* Formatted write. */
static void writef(struct generator * g, const char * input, struct node * p) {

    int i = 0;
    int l = strlen(input);

    while (i < l) {
        int ch = input[i++];
        if (ch == '~') {
            switch(input[i++]) {
                default: write_char(g, input[i - 1]); continue;
                case 'C': write_comment(g, p); continue;
                case 'f': write_block_start(g);
                          write_failure(g);
			  g->unreachable = false;
                          write_block_end(g);
                          continue;
                case 'M': write_margin(g); continue;
                case 'N': write_newline(g); continue;
                case '{': write_block_start(g); continue;
                case '}': write_block_end(g); continue;
                case 'S': write_string(g, g->S[input[i++] - '0']); continue;
                case 'B': write_b(g, g->B[input[i++] - '0']); continue;
                case 'I': write_int(g, g->I[input[i++] - '0']); continue;
                case 'V': write_varref(g, g->V[input[i++] - '0']); continue;
                case 'W': write_varname(g, g->V[input[i++] - '0']); continue;
                case 'L': write_literal_string(g, g->L[input[i++] - '0']); continue;
                case '+': g->margin++; continue;
                case '-': g->margin--; continue;
                case 'n': write_string(g, g->options->name); continue;
            }
        } else {
            write_char(g, ch);
        }
    }
}

static void w(struct generator * g, const char * s) {
    writef(g, s, 0);
}

static void generate_AE(struct generator * g, struct node * p) {
    char * s;
    switch (p->type) {
        case c_name:
            write_varref(g, p->name); break;
        case c_number:
            write_int(g, p->number); break;
        case c_maxint:
            write_string(g, "MAXINT"); break;
        case c_minint:
            write_string(g, "MININT"); break;
        case c_neg:
            write_string(g, "-"); generate_AE(g, p->right); break;
        case c_multiply:
            s = " * "; goto label0;
        case c_plus:
            s = " + "; goto label0;
        case c_minus:
            s = " - "; goto label0;
        case c_divide:
            s = " / ";
        label0:
            write_string(g, "("); generate_AE(g, p->left);
            write_string(g, s); generate_AE(g, p->right); write_string(g, ")"); break;
        case c_sizeof:
            g->V[0] = p->name;
            w(g, "(~V0.length)"); break;
        case c_cursor:
            w(g, "this.cursor"); break;
        case c_limit:
            w(g, p->mode == m_forward ? "this.limit" : "this.limit_backward"); break;
        case c_size:
            w(g, "(this.current.length)"); break;
    }
}

/* K_needed() tests to see if we really need to keep c. Not true when the
   the command does not touch the cursor. This and repeat_score() could be
   elaborated almost indefinitely.
*/

static int K_needed(struct generator * g, struct node * p) {

    while (p != 0) {
        switch (p->type) {
            case c_dollar:
            case c_leftslice:
            case c_rightslice:
            case c_mathassign:
            case c_plusassign:
            case c_minusassign:
            case c_multiplyassign:
            case c_divideassign:
            case c_eq:
            case c_ne:
            case c_gr:
            case c_ge:
            case c_ls:
            case c_le:
            case c_sliceto:
            case c_booltest:
            case c_true:
            case c_false:
            case c_debug:
                break;

            case c_call:
                if (K_needed(g, p->name->definition)) return true;
                break;

            case c_bra:
                if (K_needed(g, p->left)) return true;
                break;

            default: return true;
        }
        p = p->right;
    }
    return false;
}

static int repeat_score(struct generator * g, struct node * p) {

    int score = 0;
    while (p != 0) {
        switch (p->type) {
            case c_dollar:
            case c_leftslice:
            case c_rightslice:
            case c_mathassign:
            case c_plusassign:
            case c_minusassign:
            case c_multiplyassign:
            case c_divideassign:
            case c_eq:
            case c_ne:
            case c_gr:
            case c_ge:
            case c_ls:
            case c_le:
            case c_sliceto:   /* case c_not: must not be included here! */
            case c_debug:
                break;

            case c_call:
                score += repeat_score(g, p->name->definition);
                break;

            case c_bra:
                score += repeat_score(g, p->left);
                break;

            case c_name:
            case c_literalstring:
            case c_next:
            case c_grouping:
            case c_non:
            case c_hop:
                score = score + 1;
                break;

            default:
                score = 2;
                break;
        }
        p = p->right;
    }
    return score;
}

/* tests if an expression requires cursor reinstatement in a repeat */

static int repeat_restore(struct generator * g, struct node * p) {

    return repeat_score(g, p) >= 2;
}

static void generate_bra(struct generator * g, struct node * p) {

    write_comment(g, p);
    p = p->left;
    while (p != 0) {
        generate(g, p);
        p = p->right;
    }
}

static void generate_and(struct generator * g, struct node * p) {

    struct str * savevar = vars_newname(g);
    int keep_c = K_needed(g, p->left);

    write_comment(g, p);

    if (keep_c) write_savecursor(g, p, savevar);

    p = p->left;
    while (p != 0) {
        generate(g, p);
        if (g->unreachable) break;
        if (keep_c && p->right != 0) write_restorecursor(g, p, savevar);
        p = p->right;
    }
    str_delete(savevar);
}

static void generate_or(struct generator * g, struct node * p) {

    struct str * savevar = vars_newname(g);
    int keep_c = K_needed(g, p->left);

    int a0 = g->failure_label;
    struct str * a1 = str_copy(g->failure_str);

    int out_lab = new_label(g);
    write_comment(g, p);
    wsetlab_begin(g, out_lab);

    if (keep_c) write_savecursor(g, p, savevar);

    p = p->left;
    str_clear(g->failure_str);

    if (p == 0) {
        /* p should never be 0 after an or: there should be at least two
         * sub nodes. */
        fprintf(stderr, "Error: \"or\" node without children nodes.");
        exit (1);
    }
    while (p->right != 0) {
        g->failure_label = new_label(g);
        wsetlab_begin(g, g->failure_label);
        generate(g, p);
        if (!g->unreachable) wgotol(g, out_lab);
        wsetlab_end(g);
        g->unreachable = false;
        if (keep_c) write_restorecursor(g, p, savevar);
        p = p->right;
    }

    g->failure_label = a0;
    str_delete(g->failure_str);
    g->failure_str = a1;

    generate(g, p);
    wsetlab_end(g);
    str_delete(savevar);
}

static void generate_backwards(struct generator * g, struct node * p) {

    write_comment(g, p);
    writef(g,"~Mthis.limit_backward = this.cursor; this.cursor = this.limit;~N", p);
    generate(g, p->left);
    w(g, "~Mthis.cursor = this.limit_backward;");
}


static void generate_not(struct generator * g, struct node * p) {

    struct str * savevar = vars_newname(g);
    int keep_c = K_needed(g, p->left);

    int a0 = g->failure_label;
    struct str * a1 = str_copy(g->failure_str);

    write_comment(g, p);
    if (keep_c) {
        write_block_start(g);
        write_savecursor(g, p, savevar);
    }

    g->failure_label = new_label(g);
    str_clear(g->failure_str);

    wsetlab_begin(g, g->failure_label);

    generate(g, p->left);

    g->failure_label = a0;
    str_delete(g->failure_str);
    g->failure_str = a1;

    if (!g->unreachable) write_failure(g);

    wsetlab_end(g);
    g->unreachable = false;

    if (keep_c) write_restorecursor(g, p, savevar);
    if (keep_c) write_block_end(g);
    str_delete(savevar);
}


static void generate_try(struct generator * g, struct node * p) {

    struct str * savevar = vars_newname(g);
    int keep_c = K_needed(g, p->left);

    write_comment(g, p);
    if (keep_c) write_savecursor(g, p, savevar);

    g->failure_label = new_label(g);
    if (keep_c) restore_string(p, g->failure_str, savevar);

    wsetlab_begin(g, g->failure_label);
    generate(g, p->left);
    wsetlab_end(g);
    g->unreachable = false;

    str_delete(savevar);
}

static void generate_set(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->V[0] = p->name;
    writef(g, "~Mthis.~V0 = true;~N", p);
}

static void generate_unset(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->V[0] = p->name;
    writef(g, "~Mthis.~V0 = false;~N", p);
}

static void generate_fail(struct generator * g, struct node * p) {

    write_comment(g, p);
    generate(g, p->left);
    if (!g->unreachable) write_failure(g);
}

/* generate_test() also implements 'reverse' */

static void generate_test(struct generator * g, struct node * p) {

    struct str * savevar = vars_newname(g);
    int keep_c = K_needed(g, p->left);

    write_comment(g, p);

    if (keep_c) {
        write_savecursor(g, p, savevar);
    }

    generate(g, p->left);

    if (!g->unreachable) {
        if (keep_c) {
            write_restorecursor(g, p, savevar);
        }
    }
    str_delete(savevar);
}

static void generate_do(struct generator * g, struct node * p) {

    struct str * savevar = vars_newname(g);
    int keep_c = K_needed(g, p->left);
    write_comment(g, p);
    if (keep_c) write_savecursor(g, p, savevar);

    g->failure_label = new_label(g);
    str_clear(g->failure_str);

    wsetlab_begin(g, g->failure_label);
    generate(g, p->left);
    wsetlab_end(g);
    g->unreachable = false;

    if (keep_c) write_restorecursor(g, p, savevar);
    str_delete(savevar);
}

static void generate_GO(struct generator * g, struct node * p, int style) {

    int end_unreachable = false;
    struct str * savevar = vars_newname(g);
    int keep_c = style == 1 || repeat_restore(g, p->left);

    int a0 = g->failure_label;
    struct str * a1 = str_copy(g->failure_str);

    int golab = new_label(g);
    g->I[0] = golab;
    write_comment(g, p);
    w(g, "~Mgolab~I0: while(true)~N");
    w(g, "~{");

    if (keep_c) write_savecursor(g, p, savevar);

    g->failure_label = new_label(g);
    wsetlab_begin(g, g->failure_label);
    generate(g, p->left);

    if (g->unreachable) {
        /* Cannot break out of this loop: therefore the code after the
         * end of the loop is unreachable.*/
        end_unreachable = true;
    } else {
        /* include for goto; omit for gopast */
        if (style == 1) write_restorecursor(g, p, savevar);
        g->I[0] = golab;
        w(g, "~Mbreak golab~I0;~N");
    }
    g->unreachable = false;
    wsetlab_end(g);
    if (keep_c) write_restorecursor(g, p, savevar);

    g->failure_label = a0;
    str_delete(g->failure_str);
    g->failure_str = a1;

    write_check_limit(g, p);
    write_inc_cursor(g, p);
    write_block_end(g);
    str_delete(savevar);
    g->unreachable = end_unreachable;
}

static void generate_loop(struct generator * g, struct node * p) {

    struct str * loopvar = vars_newname(g);
    write_comment(g, p);
    g->B[0] = str_data(loopvar);
    write_declare(g, "~B0 : int", p);
    w(g, "~Mfor (var ~B0 = ");
    generate_AE(g, p->AE);
    g->B[0] = str_data(loopvar);
    writef(g, "; ~B0 > 0; ~B0--)~N", p);
    writef(g, "~{", p);

    generate(g, p->left);

    w(g, "~}");
    str_delete(loopvar);
    g->unreachable = false;
}

static void generate_repeat(struct generator * g, struct node * p, struct str * loopvar) {

    struct str * savevar = vars_newname(g);
    int keep_c = repeat_restore(g, p->left);
    int replab = new_label(g);
    g->I[0] = replab;
    write_comment(g, p);
    writef(g, "~Mreplab~I0: while(true)~N~{", p);

    if (keep_c) write_savecursor(g, p, savevar);

    g->failure_label = new_label(g);
    str_clear(g->failure_str);
    wsetlab_begin(g, g->failure_label);
    generate(g, p->left);

    if (!g->unreachable) {
        if (loopvar != 0) {
            g->B[0] = str_data(loopvar);
            w(g, "~M~B0--;~N");
        }

        g->I[0] = replab;
        w(g, "~Mcontinue replab~I0;~N");
    }

    wsetlab_end(g);
    g->unreachable = false;

    if (keep_c) write_restorecursor(g, p, savevar);

    g->I[0] = replab;
    w(g, "~Mbreak replab~I0;~N~}");
    str_delete(savevar);
}

static void generate_atleast(struct generator * g, struct node * p) {

    struct str * loopvar = vars_newname(g);
    write_comment(g, p);
    w(g, "~{");
    g->B[0] = str_data(loopvar);
    w(g, "~Mvar ~B0 = ");
    generate_AE(g, p->AE);
    w(g, ";~N");
    {
        int a0 = g->failure_label;
        struct str * a1 = str_copy(g->failure_str);

        generate_repeat(g, p, loopvar);

        g->failure_label = a0;
        str_delete(g->failure_str);
        g->failure_str = a1;
    }
    g->B[0] = str_data(loopvar);
    write_failure_if(g, "~B0 > 0", p);
    w(g, "~}");
    str_delete(loopvar);
}

static void generate_setmark(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->V[0] = p->name;
    writef(g, "~Mthis.~V0 = this.cursor;~N", p);
}

static void generate_tomark(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? ">" : "<";

    w(g, "~Mif (this.cursor ~S0 this."); generate_AE(g, p->AE); w(g, ")~N");
    write_block_start(g);
    write_failure(g);
    write_block_end(g);
    g->unreachable = false;
    w(g, "~Mthis.cursor = this."); generate_AE(g, p->AE); writef(g, ";~N", p);
}

static void generate_atmark(struct generator * g, struct node * p) {

    write_comment(g, p);
    w(g, "~Mif (this.cursor != this."); generate_AE(g, p->AE); writef(g, ")~N", p);
    write_block_start(g);
    write_failure(g);
    write_block_end(g);
    g->unreachable = false;
}


static void generate_hop(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "+" : "-";

    w(g, "~{~Mvar c : int = this.cursor ~S0 ");
    generate_AE(g, p->AE);
    w(g, ";~N");

    g->S[0] = p->mode == m_forward ? "0" : "this.limit_backward";

    write_failure_if(g, "~S0 > c || c > this.limit", p);
    writef(g, "~Mthis.cursor = c;~N", p);
    writef(g, "~}", p);
}

static void generate_delete(struct generator * g, struct node * p) {

    write_comment(g, p);
    writef(g, "~Mif (!this.slice_del())~N"
              "~M{~N"
              "~+~Mreturn false;~N~-"
              "~M}~N", p);
}


static void generate_next(struct generator * g, struct node * p) {

    write_comment(g, p);
    write_check_limit(g, p);
    write_inc_cursor(g, p);
}

static void generate_tolimit(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "this.limit" : "this.limit_backward";
    writef(g, "~Mthis.cursor = ~S0;~N", p);
}

static void generate_atlimit(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "this.limit" : "this.limit_backward";
    g->S[1] = p->mode == m_forward ? "<" : ">";
    write_failure_if(g, "this.cursor ~S1 ~S0", p);
}

static void generate_leftslice(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "this.bra" : "this.ket";
    writef(g, "~M~S0 = this.cursor;~N", p);
}

static void generate_rightslice(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "this.ket" : "this.bra";
    writef(g, "~M~S0 = this.cursor;~N", p);
}

static void generate_assignto(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->V[0] = p->name;
    writef(g, "~Mthis.~V0 = this.assign_to(this.~V0);~N", p);
}

static void generate_sliceto(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->V[0] = p->name;
    writef(g, "~Mthis.~V0 = this.slice_to(this.~V0);~N"
              "~Mif (this.~V0 == '')~N"
              "~M{~N"
              "~+~Mreturn false;~N~-"
              "~M}~N"
            , p);
}

static void generate_address(struct generator * g, struct node * p) {

    symbol * b = p->literalstring;
    if (b != 0) {
        write_literal_string(g, b);
    } else {
        write_varref(g, p->name);
    }
}

static void generate_insert(struct generator * g, struct node * p, int style) {

    int keep_c = style == c_attach;
    write_comment(g, p);
    if (p->mode == m_backward) keep_c = !keep_c;
    if (keep_c) w(g, "~{~Mvar c : int = this.cursor;~N");
    writef(g, "~Mthis.insert(this.cursor, this.cursor, ", p);
    generate_address(g, p);
    writef(g, ");~N", p);
    if (keep_c) w(g, "~Mthis.cursor = c;~N~}");
}

static void generate_assignfrom(struct generator * g, struct node * p) {

    int keep_c = p->mode == m_forward; /* like 'attach' */

    write_comment(g, p);
    if (keep_c) writef(g, "~{~Mvar c : int = this.cursor;~N", p);
    if (p->mode == m_forward) {
        writef(g, "~Mthis.insert(this.cursor, this.limit, ", p);
    } else {
        writef(g, "~Mthis.insert(this.limit_backward, this.cursor, ", p);
    }
    generate_address(g, p);
    writef(g, ");~N", p);
    if (keep_c) w(g, "~Mthis.cursor = c;~N~}");
}


static void generate_slicefrom(struct generator * g, struct node * p) {

    write_comment(g, p);
    w(g, "~Mif (!this.slice_from(");
    generate_address(g, p);
    writef(g, "))~N"
              "~M{~N"
              "~+~Mreturn false;~N~-"
              "~M}~N", p);
}

static void generate_setlimit(struct generator * g, struct node * p) {

    struct str * savevar = vars_newname(g);
    struct str * varname = vars_newname(g);
    write_comment(g, p);
    write_savecursor(g, p, savevar);
    generate(g, p->left);

    if (!g->unreachable) {
        g->B[0] = str_data(varname);
        write_declare(g, "~B0 : int", p);
        if (p->mode == m_forward) {
            w(g, "~M~B0 = this.limit - this.cursor;~N");
            w(g, "~Mthis.limit = this.cursor;~N");
        } else {
            w(g, "~M~B0 = this.limit_backward;~N");
            w(g, "~Mthis.limit_backward = this.cursor;~N");
        }
        write_restorecursor(g, p, savevar);

        if (p->mode == m_forward) {
            str_assign(g->failure_str, "this.limit += ");
            str_append(g->failure_str, varname);
            str_append_ch(g->failure_str, ';');
        } else {
            str_assign(g->failure_str, "this.limit_backward = ");
            str_append(g->failure_str, varname);
            str_append_ch(g->failure_str, ';');
        }
        generate(g, p->aux);

        if (!g->unreachable) {
            write_margin(g);
            write_str(g, g->failure_str);
            write_newline(g);
        }
    }
    str_delete(varname);
    str_delete(savevar);
}

/* dollar sets snowball up to operate on a string variable as if it were the
 * current string */
static void generate_dollar(struct generator * g, struct node * p) {

    struct str * savevar = vars_newname(g);
    write_comment(g, p);
    g->V[0] = p->name;

    str_assign(g->failure_str, "this.copy_from(");
    str_append(g->failure_str, savevar);
    str_append_string(g->failure_str, ");");
    g->B[0] = str_data(savevar);
    writef(g, "~{~M~n ~B0 = this;~N"
             "~Mthis.current = this.~V0.toString();~N"
             "~Mthis.cursor = 0;~N"
             "~Mthis.limit = (this.current.length);~N", p);
    generate(g, p->left);
    if (!g->unreachable) {
        write_margin(g);
        write_str(g, g->failure_str);
        write_newline(g);
    }
    w(g, "~}");
    str_delete(savevar);
}

static void generate_integer_assign(struct generator * g, struct node * p, char * s) {

    g->V[0] = p->name;
    g->S[0] = s;
    if (p->AE->type == c_name)
    {
        w(g, "~Mthis.~V0 ~S0 this."); generate_AE(g, p->AE); w(g, ";~N");
    }
    else
    {
        w(g, "~Mthis.~V0 ~S0 "); generate_AE(g, p->AE); w(g, ";~N");
    }
}

static void generate_integer_test(struct generator * g, struct node * p, char * s) {

    g->V[0] = p->name;
    g->S[0] = s;
    if (p->AE->type == c_name)
    {
        w(g, "~Mif (!(this.~V0 ~S0 this."); generate_AE(g, p->AE); w(g, "))~N");
    }
    else
    {
        w(g, "~Mif (!(this.~V0 ~S0 ");
        generate_AE(g, p->AE);
        w(g, "))~N");
    }
    write_block_start(g);
    write_failure(g);
    write_block_end(g);
    g->unreachable = false;
}

static void generate_call(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->V[0] = p->name;
    write_failure_if(g, "!this.~V0()", p);
}

static void generate_grouping(struct generator * g, struct node * p, int complement) {

    struct grouping * q = p->name->grouping;
    g->S[0] = p->mode == m_forward ? "" : "_b";
    g->S[1] = complement ? "out" : "in";
    g->V[0] = p->name;
    g->I[0] = q->smallest_ch;
    g->I[1] = q->largest_ch;
    if (q->no_gaps)
        write_failure_if(g, "!(this.~S1_range~S0(~I0, ~I1))", p);
    else
        write_failure_if(g, "!(this.~S1_grouping~S0(~n.~V0, ~I0, ~I1))", p);
}

static void generate_namedstring(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "" : "_b";
    g->V[0] = p->name;
    write_failure_if(g, "!(this.eq_v~S0(this.~V0))", p);
}

static void generate_literalstring(struct generator * g, struct node * p) {

    symbol * b = p->literalstring;
    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "" : "_b";
    g->I[0] = SIZE(b);
    g->L[0] = b;
    write_failure_if(g, "!(this.eq_s~S0(~I0, ~L0))", p);
}

static void generate_define(struct generator * g, struct node * p) {

    struct name * q = p->name;
    symbol stem[] = {'s', 't', 'e', 'm'};
    int find = 0;
    if (SIZE(q->b) == 4)
    {
        find = 1;
        for (int i = 0; i < 4; i++)
        {
            if (q->b[i] != stem[i])
            {
                find = 0;
                break;
            }
        }
    }
    struct str * saved_output = g->outbuf;
    struct str * saved_declarations = g->declarations;

    g->V[0] = q;
    if (find == 1)
    {
        w(g, "~N~Moverride function ~V0 () : boolean~N~M{~+~N");
    }
    else
    {
        w(g, "~N~Mfunction ~V0 () : boolean~N~M{~+~N");
    }
    g->outbuf = str_new();
    g->declarations = str_new();

    g->next_label = 0;
    g->var_number = 0;

    if (p->amongvar_needed) write_declare(g, "among_var : int", p);
    str_clear(g->failure_str);
    g->failure_label = x_return;
    g->unreachable = false;
    generate(g, p->left);
    if (!g->unreachable) w(g, "~Mreturn true;~N");
    w(g, "~}");

    str_append(saved_output, g->declarations);
    str_append(saved_output, g->outbuf);
    str_delete(g->declarations);
    str_delete(g->outbuf);
    g->declarations = saved_declarations;
    g->outbuf = saved_output;
}

static void generate_substring(struct generator * g, struct node * p) {

    struct among * x = p->among;

    write_comment(g, p);

    g->S[0] = p->mode == m_forward ? "" : "_b";
    g->I[0] = x->number;
    g->I[1] = x->literalstring_count;

    if (x->command_count == 0 && x->starter == 0) {
        write_failure_if(g, "this.find_among~S0(~n.a_~I0, ~I1) == 0", p);
    } else {
        writef(g, "~Mamong_var = this.find_among~S0(~n.a_~I0, ~I1);~N", p);
        write_failure_if(g, "among_var == 0", p);
    }
}

static void generate_among(struct generator * g, struct node * p) {

    struct among * x = p->among;
    int case_number = 1;

    if (x->substring == 0) generate_substring(g, p);
    if (x->command_count == 0 && x->starter == 0) return;

    if (x->starter != 0) generate(g, x->starter);

    p = p->left;
    if (p != 0 && p->type != c_literalstring) p = p->right;
    w(g, "~Mswitch (among_var) {~N~+");
    w(g, "~Mcase 0:~N~+");
    write_failure(g);
    g->unreachable = false;
    w(g, "~-");

    while (p != 0) {
        if (p->type == c_bra && p->left != 0) {
            g->I[0] = case_number++;
            w(g, "~Mcase ~I0:~N~+");
            generate(g, p);
            if (!g->unreachable) w(g, "~Mbreak;~N");
            w(g, "~-");
            g->unreachable = false;
        }
        p = p->right;
    }
    write_block_end(g);
}

static void generate_booltest(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->V[0] = p->name;
    write_failure_if(g, "!(this.~V0)", p);
}

static void generate_false(struct generator * g, struct node * p) {

    write_comment(g, p);
    write_failure(g);
}

static void generate_debug(struct generator * g, struct node * p) {

    write_comment(g, p);
    g->I[0] = g->debug_count++;
    g->I[1] = p->line_number;
    writef(g, "~Mthis.debug(~I0, ~I1);~N", p);
}

static void generate(struct generator * g, struct node * p) {

    int a0;
    struct str * a1;

    if (g->unreachable) return;

    a0 = g->failure_label;
    a1 = str_copy(g->failure_str);

    switch (p->type)
    {
        case c_define:        generate_define(g, p); break;
        case c_bra:           generate_bra(g, p); break;
        case c_and:           generate_and(g, p); break;
        case c_or:            generate_or(g, p); break;
        case c_backwards:     generate_backwards(g, p); break;
        case c_not:           generate_not(g, p); break;
        case c_set:           generate_set(g, p); break;
        case c_unset:         generate_unset(g, p); break;
        case c_try:           generate_try(g, p); break;
        case c_fail:          generate_fail(g, p); break;
        case c_reverse:
        case c_test:          generate_test(g, p); break;
        case c_do:            generate_do(g, p); break;
        case c_goto:          generate_GO(g, p, 1); break;
        case c_gopast:        generate_GO(g, p, 0); break;
        case c_repeat:        generate_repeat(g, p, 0); break;
        case c_loop:          generate_loop(g, p); break;
        case c_atleast:       generate_atleast(g, p); break;
        case c_setmark:       generate_setmark(g, p); break;
        case c_tomark:        generate_tomark(g, p); break;
        case c_atmark:        generate_atmark(g, p); break;
        case c_hop:           generate_hop(g, p); break;
        case c_delete:        generate_delete(g, p); break;
        case c_next:          generate_next(g, p); break;
        case c_tolimit:       generate_tolimit(g, p); break;
        case c_atlimit:       generate_atlimit(g, p); break;
        case c_leftslice:     generate_leftslice(g, p); break;
        case c_rightslice:    generate_rightslice(g, p); break;
        case c_assignto:      generate_assignto(g, p); break;
        case c_sliceto:       generate_sliceto(g, p); break;
        case c_assign:        generate_assignfrom(g, p); break;
        case c_insert:
        case c_attach:        generate_insert(g, p, p->type); break;
        case c_slicefrom:     generate_slicefrom(g, p); break;
        case c_setlimit:      generate_setlimit(g, p); break;
        case c_dollar:        generate_dollar(g, p); break;
        case c_mathassign:    generate_integer_assign(g, p, "="); break;
        case c_plusassign:    generate_integer_assign(g, p, "+="); break;
        case c_minusassign:   generate_integer_assign(g, p, "-="); break;
        case c_multiplyassign:generate_integer_assign(g, p, "*="); break;
        case c_divideassign:  generate_integer_assign(g, p, "/="); break;
        case c_eq:            generate_integer_test(g, p, "=="); break;
        case c_ne:            generate_integer_test(g, p, "!="); break;
        case c_gr:            generate_integer_test(g, p, ">"); break;
        case c_ge:            generate_integer_test(g, p, ">="); break;
        case c_ls:            generate_integer_test(g, p, "<"); break;
        case c_le:            generate_integer_test(g, p, "<="); break;
        case c_call:          generate_call(g, p); break;
        case c_grouping:      generate_grouping(g, p, false); break;
        case c_non:           generate_grouping(g, p, true); break;
        case c_name:          generate_namedstring(g, p); break;
        case c_literalstring: generate_literalstring(g, p); break;
        case c_among:         generate_among(g, p); break;
        case c_substring:     generate_substring(g, p); break;
        case c_booltest:      generate_booltest(g, p); break;
        case c_false:         generate_false(g, p); break;
        case c_true:          break;
        case c_debug:         generate_debug(g, p); break;
        default: fprintf(stderr, "%d encountered\n", p->type);
                 exit(1);
    }

    g->failure_label = a0;
    str_delete(g->failure_str);
    g->failure_str = a1;
}

static void generate_start_comment(struct generator * g) {

    w(g, "// This file was generated automatically by the Snowball to JSX compiler~N");
    w(g, "~N");
}

static void generate_class_begin(struct generator * g) {

    w(g, "import \"snowball-stemmer.jsx\";~N"
         "import \"among.jsx\";~N"
         "~N"
         " /**~N"
         "  * This class was automatically generated by a Snowball to JSX compiler~N"
         "  * It implements the stemming algorithm defined by a snowball script.~N"
         "  */~N"
         "~N"
         "class ~n extends ");
     w(g, g->options->parent_class_name);
     w(g, "~N{~N"
	  "~+~Mstatic const serialVersionUID = 1;~N"
	  "~Mstatic const methodObject = new ~n();~N"
         "~N");
}

static void generate_class_end(struct generator * g) {

    w(g, "~N}");
    w(g, "~N~N");
}

static void generate_equals(struct generator * g) {

    w(g, "~N"
         "~Mfunction equals (o : variant) : boolean {~N"
         "~+~Mreturn o instanceof ");
    w(g, g->options->name);
	 w(g, ";~N~-~M}~N"
	      "~N"
	      "~Mfunction hashCode() : int~N~M{~N"
              "~+~M//http://stackoverflow.com/questions/194846/is-there-any-kind-of-hashcode-function-in-javascript~N"
              "~Mvar classname = \"~n\";~N"
              "~Mvar hash = 0;~N"
              "~Mif (classname.length == 0) return hash;~N"
              "~Mfor (var i = 0; i < classname.length; i++) {~N"
                 "~+~Mvar char = classname.charCodeAt(i);~N"
                 "~Mhash = ((hash << 5) - hash) + char;~N"
                 "~Mhash = hash & hash; // Convert to 32bit integer~N~-"
              "~M}~N"
	      "~Mreturn hash;~N~-"
              "~M}~N");
}

static void generate_among_table(struct generator * g, struct among * x) {

    struct amongvec * v = x->b;

    g->I[0] = x->number;
    g->I[1] = x->literalstring_count;

    w(g, "~Mstatic const a_~I0 = [~N~+");
    {
        int i;
        for (i = 0; i < x->literalstring_count; i++)
        {
            g->I[0] = i;
            g->I[1] = v->i;
            g->I[2] = v->result;
            g->L[0] = v->b;
            g->S[0] = i < x->literalstring_count - 1 ? "," : "";

            w(g, "~Mnew Among(~L0, ~I1, ~I2, ");
            if (v->function != 0)
            {
                w(g, "((instance : SnowballStemmer) : boolean -> (instance as ~n).");
                write_varname(g, v->function);
                w(g, "()), ~n.methodObject)~S0~N");
            }
            else
            {
                w(g, "null, null)~S0~N");
            }
            v++;
        }
    }
    w(g, "~-~M];~N~N");
}

static void generate_amongs(struct generator * g) {

    struct among * x = g->analyser->amongs;
    while (x != 0) {
        generate_among_table(g, x);
        x = x->next;
    }
}

static void set_bit(symbol * b, int i) { b[i/8] |= 1 << i%8; }

static int bit_is_set(symbol * b, int i) { return b[i/8] & 1 << i%8; }

static void generate_grouping_table(struct generator * g, struct grouping * q) {

    int range = q->largest_ch - q->smallest_ch + 1;
    int size = (range + 7)/ 8;  /* assume 8 bits per symbol */
    symbol * b = q->b;
    symbol * map = create_b(size);
    int i;
    for (i = 0; i < size; i++) map[i] = 0;

    /* Using unicode would require revision here */

    for (i = 0; i < SIZE(b); i++) set_bit(map, b[i] - q->smallest_ch);

    q->no_gaps = true;
    for (i = 0; i < range; i++) unless (bit_is_set(map, i)) q->no_gaps = false;

    unless (q->no_gaps) {
        g->V[0] = q->name;

        w(g, "~Mstatic const ~V0 = [");
        for (i = 0; i < size; i++) {
             write_int(g, map[i]);
             if (i < size - 1) w(g, ", ");
        }
        w(g, "] : int[];~N~N");
    }
    lose_b(map);
}

static void generate_groupings(struct generator * g) {
    struct grouping * q = g->analyser->groupings;
    until (q == 0) {
        generate_grouping_table(g, q);
        q = q->next;
    }
}

static void generate_members(struct generator * g) {

    struct name * q = g->analyser->names;
    until (q == 0) {
        g->V[0] = q;
        switch (q->type) {
            case t_string:
                w(g, "    var ~W0 : string = \"\";~N");
                break;
            case t_integer:
                w(g, "    var ~W0 : int = 0;~N");
                break;
            case t_boolean:
                w(g, "    var ~W0 : boolean = false;~N");
                break;
        }
        q = q->next;
    }
    w(g, "~N");
}

static void generate_copyfrom(struct generator * g) {

    struct name * q;
    w(g, "~Mfunction copy_from (other : ~n) : void~N~M{~+~N");
    for (q = g->analyser->names; q != 0; q = q->next) {
        g->V[0] = q;
        switch (q->type) {
            case t_string:
            case t_integer:
            case t_boolean:
                w(g, "~Mthis.~W0 = other.~W0;~N");
                break;
        }
    }
    w(g, "~Msuper.copy_from(other);~N");
    w(g, "~-~M}~N");
}

static void generate_methods(struct generator * g) {

    struct node * p = g->analyser->program;
    while (p != 0) {
        generate(g, p);
        g->unreachable = false;
        p = p->right;
    }
}

extern void generate_program_jsx(struct generator * g) {

    g->outbuf = str_new();
    g->failure_str = str_new();

    generate_start_comment(g);
    generate_class_begin(g);

    generate_amongs(g);
    generate_groupings(g);

    generate_members(g);
    generate_copyfrom(g);
    generate_methods(g);
    generate_equals(g);

    generate_class_end(g);

    output_str(g->options->output_jsx, g->outbuf);
    str_delete(g->failure_str);
    str_delete(g->outbuf);
}

extern struct generator * create_generator_jsx(struct analyser * a, struct options * o) {

    NEW(generator, g);
    g->analyser = a;
    g->options = o;
    g->margin = 0;
    g->debug_count = 0;
    g->unreachable = false;
    return g;
}

extern void close_generator_jsx(struct generator * g) {

    FREE(g);
}

