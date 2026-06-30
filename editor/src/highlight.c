#include <string.h>
#include <ctype.h>
#include "editor.h"

Lang highlight_detect(const char *fn) {
  if (!fn) return LANG_NONE;
  const char *dot = strrchr(fn, '.');
  if (!dot) {
    if (strcmp(fn, "Makefile") == 0) return LANG_MAKE;
    return LANG_NONE;
  }
  dot++;
  if (!strcmp(dot, "c") || !strcmp(dot, "h"))     return LANG_C;
  if (!strcmp(dot, "cpp") || !strcmp(dot, "hpp")
      || !strcmp(dot, "cc") || !strcmp(dot, "cxx")) return LANG_C;
  if (!strcmp(dot, "go"))   return LANG_GO;
  if (!strcmp(dot, "rs"))   return LANG_RUST;
  if (!strcmp(dot, "py"))   return LANG_PYTHON;
  if (!strcmp(dot, "js") || !strcmp(dot, "ts")
      || !strcmp(dot, "jsx") || !strcmp(dot, "tsx")) return LANG_JS;
  if (!strcmp(dot, "sh") || !strcmp(dot, "bash")) return LANG_SHELL;
  if (!strcmp(dot, "json")) return LANG_JSON;
  if (!strcmp(dot, "html") || !strcmp(dot, "xml")) return LANG_HTML;
  if (!strcmp(dot, "md"))   return LANG_MD;
  return LANG_NONE;
}

static bool is_ident(int c) { return isalnum(c) || c == '_'; }

static bool is_c_keyword(const char *w, int len) {
  static const char *kw[] = {
    "auto","break","case","char","const","continue","default","do",
    "double","else","enum","extern","float","for","goto","if","int",
    "long","register","return","short","signed","sizeof","static",
    "struct","switch","typedef","union","unsigned","void","volatile",
    "while","bool","true","false","NULL","include","define",
    "ifdef","ifndef","endif","elif","else","pragma","error","warning",
    NULL
  };
  for (int i = 0; kw[i]; i++)
    if ((int)strlen(kw[i]) == len && strncmp(w, kw[i], len) == 0)
      return true;
  return false;
}

static void add_seg(HLSeg *segs, int *n, int max, int start, int len, int pair) {
  if (*n >= max) return;
  segs[*n].start = start;
  segs[*n].len = len;
  segs[*n].pair = pair;
  (*n)++;
}

/* ── C / C++ ─────────────────────────────────────────────── */
static int hl_c(const char *line, int *state, HLSeg *segs, int max) {
  int n = 0, i = 0, len = strlen(line);
  int block = *state; // 1 = in /* */ comment

  while (i < len) {
    if (block) {
      int start = i;
      if (line[i] == '*' && line[i+1] == '/') {
        add_seg(segs, &n, max, start, 2, CP_COMMENT);
        block = 0; i += 2;
        continue;
      }
      while (i < len && !(line[i] == '*' && line[i+1] == '/')) i++;
      if (i >= len) {
        add_seg(segs, &n, max, start, len - start, CP_COMMENT);
      } else {
        add_seg(segs, &n, max, start, i - start + 2, CP_COMMENT);
        block = 0; i += 2;
      }
      continue;
    }

    // preprocessor
    if (i == 0 && line[i] == '#') {
      add_seg(segs, &n, max, i, len - i, CP_PREPROC);
      break;
    }

    // line comment
    if (line[i] == '/' && line[i+1] == '/') {
      add_seg(segs, &n, max, i, len - i, CP_COMMENT);
      break;
    }

    // block comment start
    if (line[i] == '/' && line[i+1] == '*') {
      int start = i;
      i += 2;
      while (i < len && !(line[i] == '*' && line[i+1] == '/')) i++;
      if (i >= len) {
        add_seg(segs, &n, max, start, len - start, CP_COMMENT);
        block = 1;
      } else {
        add_seg(segs, &n, max, start, i - start + 2, CP_COMMENT);
        i += 2;
      }
      continue;
    }

    // string
    if (line[i] == '"') {
      int start = i; i++;
      while (i < len && line[i] != '"') {
        if (line[i] == '\\') i++;
        if (i < len) i++;
      }
      if (i < len) i++;
      add_seg(segs, &n, max, start, i - start, CP_STRING);
      continue;
    }

    // char literal
    if (line[i] == '\'') {
      int start = i; i++;
      while (i < len && line[i] != '\'') {
        if (line[i] == '\\') i++;
        if (i < len) i++;
      }
      if (i < len) i++;
      add_seg(segs, &n, max, start, i - start, CP_STRING);
      continue;
    }

    // number
    if (isdigit(line[i]) || (line[i] == '.' && isdigit(line[i+1]))) {
      int start = i;
      if (line[i] == '0' && (line[i+1] == 'x' || line[i+1] == 'X')) {
        i += 2;
        while (isxdigit(line[i])) i++;
      } else {
        while (isdigit(line[i]) || line[i] == '.') i++;
        if (line[i] == 'f' || line[i] == 'F' || line[i] == 'L'
            || line[i] == 'u' || line[i] == 'U') i++;
      }
      add_seg(segs, &n, max, start, i - start, CP_NUMBER);
      continue;
    }

    // identifier or keyword
    if (isalpha(line[i]) || line[i] == '_') {
      int start = i;
      while (is_ident(line[i])) i++;
      int kw = is_c_keyword(line + start, i - start);
      add_seg(segs, &n, max, start, i - start, kw ? CP_KEYWORD : -1);
      continue;
    }

    // operators
    const char *ops = "+-*/%<>=!&|^~?:;,.[](){}";
    if (strchr(ops, line[i])) {
      int start = i;
      // check double-char ops
      if ((line[i] == '+' && line[i+1] == '+') ||
          (line[i] == '-' && line[i+1] == '-') ||
          (line[i] == '-' && line[i+1] == '>') ||
          (line[i] == '<' && line[i+1] == '<') ||
          (line[i] == '>' && line[i+1] == '>') ||
          (line[i] == '=' && line[i+1] == '=') ||
          (line[i] == '!' && line[i+1] == '=') ||
          (line[i] == '<' && line[i+1] == '=') ||
          (line[i] == '>' && line[i+1] == '=') ||
          (line[i] == '&' && line[i+1] == '&') ||
          (line[i] == '|' && line[i+1] == '|') ||
          (line[i] == ':' && line[i+1] == ':'))
        add_seg(segs, &n, max, start, 2, CP_OPERATOR), i += 2;
      else
        add_seg(segs, &n, max, start, 1, CP_OPERATOR), i++;
      continue;
    }

    i++;
  }

  *state = block;
  return n;
}

/* ── Python ──────────────────────────────────────────────── */
static bool is_py_kw(const char *w, int len) {
  static const char *kw[] = {
    "False","None","True","and","as","assert","async","await","break",
    "class","continue","def","del","elif","else","except","finally",
    "for","from","global","if","import","in","is","lambda","nonlocal",
    "not","or","pass","raise","return","try","while","with","yield",
    "self","print",NULL
  };
  for (int i = 0; kw[i]; i++)
    if ((int)strlen(kw[i]) == len && strncmp(w, kw[i], len) == 0)
      return true;
  return false;
}

static int hl_python(const char *line, int *state, HLSeg *segs, int max) {
  int n = 0, i = 0, len = strlen(line);
  int in_triple = *state; // 1 = in """ / '''

  if (in_triple) {
    int start = i;
    while (i < len) {
      if ((line[i] == '"' && line[i+1] == '"' && line[i+2] == '"') ||
          (line[i] == '\'' && line[i+1] == '\'' && line[i+2] == '\'')) {
        in_triple = 0; i += 3; break;
      }
      i++;
    }
    add_seg(segs, &n, max, start, i - start, CP_STRING);
    if (in_triple) { *state = 1; return n; }
  }

  while (i < len) {
    // comment
    if (line[i] == '#') {
      add_seg(segs, &n, max, i, len - i, CP_COMMENT);
      break;
    }
    // triple-quoted string
    if ((line[i] == '"' && line[i+1] == '"' && line[i+2] == '"') ||
        (line[i] == '\'' && line[i+1] == '\'' && line[i+2] == '\'')) {
      int start = i; i += 3;
      while (i < len) {
        if ((line[i] == '"' && line[i+1] == '"' && line[i+2] == '"') ||
            (line[i] == '\'' && line[i+1] == '\'' && line[i+2] == '\'')) {
          i += 3; break;
        }
        i++;
      }
      if (i >= len && start == 0) in_triple = 1;
      add_seg(segs, &n, max, start, i - start + (in_triple ? 0 : 0), CP_STRING);
      if (in_triple) break;
      continue;
    }
    // f-string or regular string
    if (line[i] == '"' || line[i] == '\'') {
      char q = line[i]; int start = i; i++;
      while (i < len && line[i] != q) { if (line[i] == '\\') i++; i++; }
      if (i < len) i++;
      add_seg(segs, &n, max, start, i - start, CP_STRING);
      continue;
    }
    // decorator
    if (i == 0 && line[i] == '@') {
      add_seg(segs, &n, max, i, len, CP_PREPROC);
      break;
    }
    // number
    if (isdigit(line[i]) || (line[i] == '.' && isdigit(line[i+1]))) {
      int start = i;
      while (isxdigit(line[i]) || line[i] == '.' || line[i] == 'x' || line[i] == 'X'
             || line[i] == 'e' || line[i] == 'E' || line[i] == '_'
             || line[i] == '+' || line[i] == '-') i++;
      add_seg(segs, &n, max, start, i - start, CP_NUMBER);
      continue;
    }
    // identifier / keyword
    if (isalpha(line[i]) || line[i] == '_') {
      int start = i;
      while (is_ident(line[i])) i++;
      int kw = is_py_kw(line + start, i - start);
      int pair = -1;
      if (kw) pair = CP_KEYWORD;
      else if (start > 0 && line[start-1] == '.') pair = CP_FUNC; // method call
      else if (line[i] == '(') pair = CP_FUNC; // function call
      add_seg(segs, &n, max, start, i - start, pair);
      continue;
    }
    // operator
    if (strchr("+-*/%=<>!&|^~@:;,.[](){}", line[i])) {
      add_seg(segs, &n, max, i, 1, CP_OPERATOR), i++;
      continue;
    }
    i++;
  }
  *state = in_triple;
  return n;
}

/* ── JavaScript / TypeScript ─────────────────────────────── */
static bool is_js_kw(const char *w, int len) {
  static const char *kw[] = {
    "break","case","catch","class","const","continue","debugger",
    "default","delete","do","else","export","extends","finally",
    "for","function","if","import","in","instanceof","let","new",
    "of","return","super","switch","this","throw","try","typeof",
    "var","void","while","with","yield","async","await","from",
    "true","false","null","undefined","static","get","set",
    "interface","type","enum","implements","private","protected",
    "public","as","any","boolean","number","string","never","unknown",
    "readonly","abstract","keyof","infer","declare","module",
    "require","console","document","window","process",NULL
  };
  for (int i = 0; kw[i]; i++)
    if ((int)strlen(kw[i]) == len && strncmp(w, kw[i], len) == 0)
      return true;
  return false;
}

static int hl_js(const char *line, int *state, HLSeg *segs, int max) {
  int n = 0, i = 0, len = strlen(line);
  int block = *state;

  while (i < len) {
    if (block) {
      int start = i;
      while (i < len && !(line[i] == '*' && line[i+1] == '/')) i++;
      if (i >= len) { add_seg(segs, &n, max, start, len-start, CP_COMMENT); }
      else { add_seg(segs, &n, max, start, i-start+2, CP_COMMENT); i+=2; block=0; }
      continue;
    }
    // line comment
    if (line[i] == '/' && line[i+1] == '/') {
      add_seg(segs, &n, max, i, len - i, CP_COMMENT);
      break;
    }
    // block comment
    if (line[i] == '/' && line[i+1] == '*') {
      int start = i; i += 2; block = 1;
      while (i < len && !(line[i] == '*' && line[i+1] == '/')) i++;
      if (i >= len) { add_seg(segs, &n, max, start, len-start, CP_COMMENT); }
      else { add_seg(segs, &n, max, start, i-start+2, CP_COMMENT); i+=2; block=0; }
      continue;
    }
    // template literal
    if (line[i] == '`') {
      int start = i; i++;
      while (i < len && line[i] != '`') { if (line[i] == '\\') i++; i++; }
      if (i < len) i++;
      add_seg(segs, &n, max, start, i - start, CP_STRING);
      continue;
    }
    // string
    if (line[i] == '"' || line[i] == '\'') {
      char q = line[i]; int start = i; i++;
      while (i < len && line[i] != q) { if (line[i] == '\\') i++; i++; }
      if (i < len) i++;
      add_seg(segs, &n, max, start, i - start, CP_STRING);
      continue;
    }
    // regex (simplified: /.../)
    // FIX: skip regex for now to avoid false positives

    // number
    if (isdigit(line[i]) || (line[i] == '.' && isdigit(line[i+1]))) {
      int start = i;
      while (isxdigit(line[i]) || line[i] == '.' || line[i] == 'x' || line[i] == 'X'
             || line[i] == 'e' || line[i] == 'E' || line[i] == '_') i++;
      if (line[i] == 'n') i++;
      add_seg(segs, &n, max, start, i - start, CP_NUMBER);
      continue;
    }
    // identifier
    if (isalpha(line[i]) || line[i] == '_' || line[i] == '$') {
      int start = i;
      while (is_ident(line[i]) || line[i] == '$') i++;
      int kw = is_js_kw(line + start, i - start);
      int pair = -1;
      if (kw) pair = CP_KEYWORD;
      else if (line[i] == '(') pair = CP_FUNC;
      add_seg(segs, &n, max, start, i - start, pair);
      continue;
    }
    // operator
    if (strchr("+-*/%=<>!&|^~?:;,.[](){}", line[i])) {
      add_seg(segs, &n, max, i, 1, CP_OPERATOR), i++;
      continue;
    }
    i++;
  }
  *state = block;
  return n;
}

/* ── Go ──────────────────────────────────────────────────── */
static bool is_go_kw(const char *w, int len) {
  static const char *kw[] = {
    "break","case","chan","const","continue","default","defer","else",
    "fallthrough","for","func","go","goto","if","import","interface",
    "map","package","range","return","select","struct","switch","type",
    "var","true","false","nil","int","int8","int16","int32","int64",
    "uint","uint8","uint16","uint32","uint64","float32","float64",
    "complex64","complex128","byte","rune","string","bool","error",
    "make","len","cap","append","copy","close","delete","panic",
    "recover","print","println","iota",NULL
  };
  for (int i = 0; kw[i]; i++)
    if ((int)strlen(kw[i]) == len && strncmp(w, kw[i], len) == 0)
      return true;
  return false;
}

static int hl_go(const char *line, int *state, HLSeg *segs, int max) {
  int n = 0, i = 0, len = strlen(line);
  int block = *state;

  while (i < len) {
    if (block) {
      int start = i;
      while (i < len && !(line[i] == '*' && line[i+1] == '/')) i++;
      if (i >= len) { add_seg(segs, &n, max, start, len-start, CP_COMMENT); }
      else { add_seg(segs, &n, max, start, i-start+2, CP_COMMENT); i+=2; block=0; }
      continue;
    }
    if (line[i] == '/' && line[i+1] == '/') {
      add_seg(segs, &n, max, i, len-i, CP_COMMENT); break;
    }
    if (line[i] == '/' && line[i+1] == '*') {
      int start = i; i += 2; block = 1;
      while (i < len && !(line[i] == '*' && line[i+1] == '/')) i++;
      if (i >= len) { add_seg(segs, &n, max, start, len-start, CP_COMMENT); }
      else { add_seg(segs, &n, max, start, i-start+2, CP_COMMENT); i+=2; block=0; }
      continue;
    }
    // raw string
    if (line[i] == '`') {
      int start = i; i++;
      while (i < len && line[i] != '`') i++;
      if (i < len) i++;
      add_seg(segs, &n, max, start, i-start, CP_STRING);
      continue;
    }
    if (line[i] == '"') {
      int start = i; i++;
      while (i < len && line[i] != '"') { if (line[i]=='\\') i++; i++; }
      if (i < len) i++;
      add_seg(segs, &n, max, start, i-start, CP_STRING);
      continue;
    }
    if (isdigit(line[i]) || (line[i]=='.' && isdigit(line[i+1]))) {
      int start = i;
      while (isxdigit(line[i]) || line[i]=='.' || line[i]=='x' || line[i]=='X'
             || line[i]=='e' || line[i]=='E' || line[i]=='_' || line[i]=='i') i++;
      add_seg(segs, &n, max, start, i-start, CP_NUMBER);
      continue;
    }
    if (isalpha(line[i]) || line[i]=='_') {
      int start = i;
      while (is_ident(line[i])) i++;
      int kw = is_go_kw(line+start, i-start);
      int pair = -1;
      if (kw) pair = CP_KEYWORD;
      else if (isupper(line[start])) pair = CP_TYPE;
      else if (line[i] == '(') pair = CP_FUNC;
      add_seg(segs, &n, max, start, i-start, pair);
      continue;
    }
    if (strchr("+-*/%=<>!&|^~?:;,.[](){}", line[i])) {
      add_seg(segs, &n, max, i, 1, CP_OPERATOR), i++;
      continue;
    }
    i++;
  }
  *state = block;
  return n;
}

/* ── Shell / Bash ────────────────────────────────────────── */
static int hl_shell(const char *line, int *state, HLSeg *segs, int max) {
  int n = 0, i = 0, len = strlen(line);
  (void)state;

  while (i < len) {
    if (line[i] == '#') {
      add_seg(segs, &n, max, i, len-i, CP_COMMENT); break;
    }
    if (line[i] == '"') {
      int start = i; i++;
      while (i < len && line[i] != '"') { if (line[i]=='\\') i++; i++; }
      if (i < len) i++;
      add_seg(segs, &n, max, start, i-start, CP_STRING);
      continue;
    }
    if (line[i] == '\'') {
      int start = i; i++;
      while (i < len && line[i] != '\'') i++;
      if (i < len) i++;
      add_seg(segs, &n, max, start, i-start, CP_STRING);
      continue;
    }
    // variable
    if (line[i] == '$' && (isalpha(line[i+1]) || line[i+1]=='_'
                           || line[i+1]=='{' || line[i+1]=='(')) {
      int start = i; i++;
      if (line[i] == '{') { while (i < len && line[i]!='}') i++; if(i<len) i++; }
      else if (line[i] == '(') { while (i<len && line[i]!=')') i++; if(i<len) i++; }
      else while (is_ident(line[i])) i++;
      add_seg(segs, &n, max, start, i-start, CP_CONST);
      continue;
    }
    // common commands (first word on line or after | & ;)
    if ((i == 0 || line[i-1] == '|' || line[i-1] == '&' || line[i-1] == ';'
         || line[i-1] == ' ' || line[i-1] == '\t') && isalpha(line[i])) {
      int start = i;
      while (isalpha(line[i]) || line[i]=='-') i++;
      static const char *cmds[] = {
        "echo","cd","ls","cat","grep","sed","awk","curl","wget",
        "git","make","gcc","g++","python","node","npm","docker",
        "ssh","scp","rsync","tar","zip","unzip","chmod","chown",
        "sudo","apt","brew","pip","cargo","go","rustc","export",
        "source","alias","unalias","type","which","env","printenv",
        "exit","return","if","then","else","elif","fi","for","while",
        "do","done","case","esac","function","local","readonly",
        NULL
      };
      int kw = 0;
      for (int j = 0; cmds[j]; j++)
        if ((int)strlen(cmds[j]) == i-start && strncmp(line+start, cmds[j], i-start)==0) {
          kw = 1; break;
        }
      add_seg(segs, &n, max, start, i-start, kw ? CP_KEYWORD : -1);
      continue;
    }
    if (strchr("|&;<>!()[]{}", line[i])) {
      add_seg(segs, &n, max, i, 1, CP_OPERATOR), i++;
      continue;
    }
    i++;
  }
  return n;
}

/* ── Makefile ────────────────────────────────────────────── */
static int hl_make(const char *line, int *state, HLSeg *segs, int max) {
  int n = 0, i = 0, len = strlen(line);
  (void)state;

  if (line[0] == '#') {
    add_seg(segs, &n, max, 0, len, CP_COMMENT);
    return n;
  }
  // target:
  if (isalpha(line[0]) || line[0] == '.' || line[0] == '_') {
    while (i < len && line[i] != ':') i++;
    if (line[i] == ':') {
      add_seg(segs, &n, max, 0, i, CP_FUNC);
      add_seg(segs, &n, max, i, 1, CP_OPERATOR);
      i++;
    } else { i = 0; }
  }
  while (i < len) {
    if (line[i] == '#') {
      add_seg(segs, &n, max, i, len-i, CP_COMMENT); break;
    }
    if (line[i] == '$' && (line[i+1]=='(' || line[i+1]=='{' || isalpha(line[i+1]))) {
      int start = i; i++;
      if (line[i]=='(' || line[i]=='{') {
        char close = (line[i]=='(') ? ')' : '}';
        while (i<len && line[i]!=close) i++;
        if (i<len) i++;
      } else while (is_ident(line[i])) i++;
      add_seg(segs, &n, max, start, i-start, CP_CONST);
      continue;
    }
    if (line[i] == '\t' && n == 0) {
      int start = i;
      while (i < len && line[i] != '#') i++;
      add_seg(segs, &n, max, start, i-start, -1);
      continue;
    }
    i++;
  }
  return n;
}

/* ── JSON ────────────────────────────────────────────────── */
static int hl_json(const char *line, int *state, HLSeg *segs, int max) {
  int n = 0, i = 0, len = strlen(line);
  (void)state;

  while (i < len) {
    if (line[i] == '"') {
      int start = i, is_key = (n == 0);
      // check if previous non-space was { or ,
      if (!is_key) {
        for (int j = i-1; j >= 0; j--)
          if (line[j] != ' ') { is_key = (line[j]=='{' || line[j]==','); break; }
      }
      i++;
      while (i < len && line[i] != '"') { if (line[i]=='\\') i++; i++; }
      if (i < len) i++;
      add_seg(segs, &n, max, start, i-start, is_key ? CP_TYPE : CP_STRING);
      continue;
    }
    if (isdigit(line[i]) || line[i]=='-') {
      int start = i; i++;
      while (isdigit(line[i]) || line[i]=='.' || line[i]=='e' || line[i]=='E'
             || line[i]=='+' || line[i]=='-') i++;
      add_seg(segs, &n, max, start, i-start, CP_NUMBER);
      continue;
    }
    if (strncmp(line+i, "true", 4)==0 || strncmp(line+i, "null", 4)==0) {
      add_seg(segs, &n, max, i, 4, CP_CONST); i+=4; continue;
    }
    if (strncmp(line+i, "false", 5)==0) {
      add_seg(segs, &n, max, i, 5, CP_CONST); i+=5; continue;
    }
    if (strchr("{}[]:,", line[i])) {
      add_seg(segs, &n, max, i, 1, CP_OPERATOR), i++;
      continue;
    }
    i++;
  }
  return n;
}

/* ── HTML / XML ──────────────────────────────────────────── */
static int hl_html(const char *line, int *state, HLSeg *segs, int max) {
  int n = 0, i = 0, len = strlen(line);
  (void)state;

  while (i < len) {
    if (line[i] == '<') {
      int start = i;
      if (line[i+1] == '!' && line[i+2] == '-' && line[i+3] == '-') {
        // comment
        i += 4;
        while (i < len && !(line[i]=='-' && line[i+1]=='-' && line[i+2]=='>')) i++;
        if (i < len) i += 3;
        add_seg(segs, &n, max, start, i-start, CP_COMMENT);
        continue;
      }
      // tag
      while (i < len && line[i] != '>') {
        if (line[i] == '"') { i++; while(i<len && line[i]!='"') i++; }
        i++;
      }
      if (i < len) i++;
      add_seg(segs, &n, max, start, i-start, CP_KEYWORD);
      continue;
    }
    // text content
    {
      int start = i;
      while (i < len && line[i] != '<') i++;
      if (i > start) add_seg(segs, &n, max, start, i-start, -1);
    }
  }
  return n;
}

/* ── Markdown ────────────────────────────────────────────── */
static int hl_md(const char *line, int *state, HLSeg *segs, int max) {
  int n = 0, i = 0, len = strlen(line);
  int in_code = *state; // 1 = in ``` block

  if (in_code) {
    if (strncmp(line, "```", 3) == 0) {
      add_seg(segs, &n, max, 0, len, CP_KEYWORD);
      *state = 0; return n;
    }
    add_seg(segs, &n, max, 0, len, CP_COMMENT);
    return n;
  }

  // heading
  if (line[0] == '#') {
    while (line[i] == '#') i++;
    add_seg(segs, &n, max, 0, i, CP_OPERATOR);
    if (i < len) add_seg(segs, &n, max, i, len-i, CP_FUNC);
    return n;
  }
  // code fence
  if (strncmp(line, "```", 3) == 0) {
    add_seg(segs, &n, max, 0, len, CP_KEYWORD);
    *state = 1; return n;
  }
  // inline code, bold, italic etc (simplified)
  while (i < len) {
    if (line[i] == '`') {
      int start = i; i++;
      while (i < len && line[i] != '`') i++;
      if (i < len) i++;
      add_seg(segs, &n, max, start, i-start, CP_CONST);
      continue;
    }
    if (line[i] == '[') { // link
      int start = i; i++;
      while (i < len && line[i] != ']') i++;
      if (i < len) i++;
      if (line[i] == '(') { i++; while(i<len && line[i]!=')') i++; if(i<len) i++; }
      add_seg(segs, &n, max, start, i-start, CP_STRING);
      continue;
    }
    if (strchr("*-+", line[i]) && i == 0) {
      add_seg(segs, &n, max, i, 1, CP_OPERATOR); i++;
      continue;
    }
    i++;
  }
  return n;
}

/* ── Rust ────────────────────────────────────────────────── */
static bool is_rs_kw(const char *w, int len) {
  static const char *kw[] = {
    "as","break","const","continue","crate","else","enum","extern",
    "false","fn","for","if","impl","in","let","loop","match","mod",
    "move","mut","pub","ref","return","self","Self","static","struct",
    "super","trait","true","type","unsafe","use","where","while",
    "async","await","dyn","abstract","become","box","do","final",
    "macro","override","priv","typeof","unsized","virtual","yield",
    "i8","i16","i32","i64","i128","isize","u8","u16","u32","u64",
    "u128","usize","f32","f64","bool","char","str","String","Vec",
    "Option","Result","Some","None","Ok","Err","Box","Rc","Arc",
    "HashMap","HashSet","println","format","panic","assert",
    NULL
  };
  for (int i = 0; kw[i]; i++)
    if ((int)strlen(kw[i]) == len && strncmp(w, kw[i], len) == 0)
      return true;
  return false;
}

static int hl_rust(const char *line, int *state, HLSeg *segs, int max) {
  int n = 0, i = 0, len = strlen(line);
  int block = *state;

  while (i < len) {
    if (block) {
      int start = i;
      while (i < len && !(line[i] == '*' && line[i+1] == '/')) i++;
      if (i >= len) { add_seg(segs, &n, max, start, len-start, CP_COMMENT); }
      else { add_seg(segs, &n, max, start, i-start+2, CP_COMMENT); i+=2; block=0; }
      continue;
    }
    if (line[i] == '/' && line[i+1] == '/') {
      add_seg(segs, &n, max, i, len-i, CP_COMMENT); break;
    }
    if (line[i] == '/' && line[i+1] == '*') {
      int start = i; i += 2; block = 1;
      while (i < len && !(line[i] == '*' && line[i+1] == '/')) i++;
      if (i >= len) { add_seg(segs, &n, max, start, len-start, CP_COMMENT); }
      else { add_seg(segs, &n, max, start, i-start+2, CP_COMMENT); i+=2; block=0; }
      continue;
    }
    if (line[i] == '"') {
      int start = i; i++;
      while (i < len && line[i] != '"') { if (line[i]=='\\') i++; i++; }
      if (i < len) i++;
      add_seg(segs, &n, max, start, i-start, CP_STRING);
      continue;
    }
    // raw string r#"..."# or r"..."
    if (line[i] == 'r' && (line[i+1] == '"' || line[i+1] == '#')) {
      int start = i, hashes = 0;
      i++;
      while (line[i] == '#') { hashes++; i++; }
      if (line[i] == '"') {
        i++;
        while (i < len) {
          if (line[i] == '"') {
            int h = 0; int j = i+1;
            while (line[j] == '#') { h++; j++; }
            if (h == hashes) { i = j; break; }
          }
          i++;
        }
        add_seg(segs, &n, max, start, i-start, CP_STRING);
        continue;
      }
      i = start;
    }
    if (line[i] == '\'') {
      int start = i; i++;
      while (i < len && line[i] != '\'') i++;
      if (i < len) i++;
      add_seg(segs, &n, max, start, i-start, CP_STRING);
      continue;
    }
    // lifetime
    if (line[i] == '\'' && isalpha(line[i+1])) {
      int start = i; i++;
      while (is_ident(line[i])) i++;
      add_seg(segs, &n, max, start, i-start, CP_TYPE);
      continue;
    }
    if (isdigit(line[i]) || (line[i]=='.' && isdigit(line[i+1]))) {
      int start = i;
      while (isxdigit(line[i]) || line[i]=='.' || line[i]=='x' || line[i]=='X'
             || line[i]=='e' || line[i]=='E' || line[i]=='_' || line[i]=='i'
             || line[i]=='u' || line[i]=='f') i++;
      add_seg(segs, &n, max, start, i-start, CP_NUMBER);
      continue;
    }
    if (line[i] == '#' && line[i+1] == '!') {
      add_seg(segs, &n, max, i, len-i, CP_PREPROC); break;
    }
    if (line[i] == '#' && line[i+1] == '[') {
      int start = i;
      while (i < len && !(line[i]==']' && line[i+1]==']')) i++;
      if (i < len) i += 2;
      add_seg(segs, &n, max, start, i-start, CP_PREPROC);
      continue;
    }
    if (isalpha(line[i]) || line[i]=='_') {
      int start = i;
      while (is_ident(line[i]) || line[i]=='!') i++;
      int kw = is_rs_kw(line+start, i-start);
      int pair = -1;
      if (kw) pair = CP_KEYWORD;
      else if (line[i] == '(') pair = CP_FUNC;
      else if (isupper(line[start]) || (start>0 && line[start-1]==':'
               && line[start-2]==':')) pair = CP_TYPE;
      add_seg(segs, &n, max, start, i-start, pair);
      continue;
    }
    if (strchr("+-*/%=<>!&|^~?:;,.@[](){}", line[i])) {
      add_seg(segs, &n, max, i, 1, CP_OPERATOR), i++;
      continue;
    }
    i++;
  }
  *state = block;
  return n;
}

/* ── Dispatch ────────────────────────────────────────────── */
int highlight_line(const char *line, Lang lang, int *state, HLSeg *segs, int max) {
  if (!line || lang == LANG_NONE) return 0;
  switch (lang) {
    case LANG_C:      return hl_c(line, state, segs, max);
    case LANG_GO:     return hl_go(line, state, segs, max);
    case LANG_RUST:   return hl_rust(line, state, segs, max);
    case LANG_PYTHON: return hl_python(line, state, segs, max);
    case LANG_JS:     return hl_js(line, state, segs, max);
    case LANG_SHELL:  return hl_shell(line, state, segs, max);
    case LANG_MAKE:   return hl_make(line, state, segs, max);
    case LANG_JSON:   return hl_json(line, state, segs, max);
    case LANG_HTML:   return hl_html(line, state, segs, max);
    case LANG_MD:     return hl_md(line, state, segs, max);
    default: return 0;
  }
}
