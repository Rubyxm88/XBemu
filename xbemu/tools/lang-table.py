#!/usr/bin/env python3
"""XBemu's translation table, data/xbemu-lang.tsv

  lang-table.py update   add the texts found in the UI sources as new rows
                         (translations kept; rows no longer found are kept
                         and listed)
  lang-table.py check    every translation keeps the English text's
                         placeholders (%s, %d, ...) in the same order, and
                         how many texts each language still lacks
  lang-table.py list     the texts found in the sources, one per line

The table: a header line "en<TAB>de<TAB>fr<TAB>es<TAB>it<TAB>pt", then one
row per English text. In cells, \\n is a new line, \\t a tab, \\\\ a
backslash. Lines starting with # are notes. An empty cell shows English.
The app matches whole texts as they are drawn (see ui/xui/i18n.hh): a label's
"##id" part is not part of the text, and icons or spaces around it are kept.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
TABLE = os.path.join(ROOT, 'data', 'xbemu-lang.tsv')
LANGS = ['en', 'de', 'fr', 'es', 'it', 'pt']

# UI sources whose texts players see
SOURCES = [
    'ui/xui/actions.cc', 'ui/xui/dash-install.cc', 'ui/xui/disc-refresh.cc',
    'ui/xui/full-report.cc', 'ui/xui/game-cache.cc', 'ui/xui/guide.cc',
    'ui/xui/i18n.cc', 'ui/xui/main-menu.cc', 'ui/xui/main.cc',
    'ui/xui/menubar.cc', 'ui/xui/notifications.cc', 'ui/xui/oxos-update.cc',
    'ui/xui/popup-menu.cc', 'ui/xui/scene-manager.cc',
    'ui/xui/self-update.cc', 'ui/xui/shader-cache.cc',
    'ui/xui/snapshot-manager.cc', 'ui/xui/versions.cc', 'ui/xui/welcome.cc',
    'ui/xui/widgets.cc', 'ui/xui/xbcheck.cc', 'ui/xemu-xbcheck.c',
]

# Calls whose texts are never drawn (logs, files, comparisons)
NOT_DRAWN = {
    'fprintf', 'printf', 'g_printerr', 'g_print', 'error_report',
    'warn_report', 'info_report', 'error_setg', 'DPRINTF', 'qemu_log',
    'getenv', 'g_getenv', 'strcmp', 'strncmp', 'strcasecmp', 'g_strcmp0',
    'g_str_has_prefix', 'g_str_has_suffix', 'strstr', 'strchr', 'fopen',
    'g_build_filename', 'g_file_test', 'SDL_OpenURL', 'curl_easy_setopt',
    'xemu_settings_set_string', 'value', 'find', 'rfind', 'contains',
    'count', 'compare', 'starts_with', 'ends_with', 'StartsWith', 'EndsWith',
    'Get', 'Set', 'setenv', 'g_setenv', 'g_strsplit', 'g_strsplit_set',
    'PushID', 'GetID', 'BeginChild', 'OpenPopup', 'BeginPopup',
    'IsPopupOpen', 'BeginTable', 'TableSetupColumn', 'sscanf', 'strtol',
    'g_key_file_get_string', 'g_key_file_set_string', 'Keys', 'KeyValue',
    'json', 'at', 'emplace', 'insert', 'g_regex_new', 'system',
    'g_spawn_command_line_async', 'ShellExecuteA', 'ShellExecuteW',
    'g_strdup_printf_log', 'LogLine', 'Log', 'xemu_diag_log',
}

STR_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def c_unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            n = s[i + 1]
            i += 2
            if n == 'n':
                out.append('\n')
            elif n == 't':
                out.append('\t')
            elif n in '"\\\'?':
                out.append(n)
            elif n == 'x':
                m = re.match(r'[0-9a-fA-F]{1,2}', s[i:])
                out.append(chr(int(m.group(0), 16)))
                i += len(m.group(0))
            elif n in '01234567':
                m = re.match(r'[0-7]{1,3}', s[i - 1:])
                out.append(chr(int(m.group(0), 8)))
                i += len(m.group(0)) - 1
            else:
                out.append(n)
        else:
            out.append(c)
            i += 1
    # \x escapes are UTF-8 bytes
    raw = ''.join(out)
    try:
        return raw.encode('latin-1').decode('utf-8')
    except (UnicodeEncodeError, UnicodeDecodeError):
        return raw


def strip_comments(src):
    """Comments out, strings and line breaks kept (so lines still count)"""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith('//', i):
            j = src.find('\n', i)
            i = n if j < 0 else j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append('\n' * src.count('\n', i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def texts_in(path):
    src = strip_comments(open(os.path.join(ROOT, path), encoding='utf-8',
                              errors='replace').read())
    found = []
    stack = []  # names of the calls we are inside
    i, n = 0, len(src)
    ident_re = re.compile(r'[A-Za-z_][A-Za-z_0-9]*')
    last_ident = None
    while i < n:
        c = src[i]
        if c == '#' and (i == 0 or src[i - 1] == '\n'):
            j = src.find('\n', i)  # preprocessor line: skip (#include "x")
            if src.startswith('#define', i):
                i += 1
                continue
            i = n if j < 0 else j
            continue
        if c == '"':
            # A run of adjacent literals is one text
            line = src.count('\n', 0, i) + 1
            parts = []
            while i < n and src[i] == '"':
                m = STR_RE.match(src, i)
                if not m:
                    i += 1
                    break
                parts.append(m.group(1))
                i = m.end()
                while i < n and src[i] in ' \t\r\n':
                    i += 1
            text = ''.join(c_unescape(x) for x in parts)
            if not any(s in NOT_DRAWN for s in stack):
                # "A\0B\0" lists (combo items) are separate texts
                for item in text.split('\0'):
                    found.append((item, line))
            last_ident = None
            continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == '\\' else 1
            i = j + 1
            continue
        m = ident_re.match(src, i)
        if m:
            last_ident = m.group(0)
            i = m.end()
            continue
        if c == '(':
            stack.append(last_ident or '')
        elif c == ')':
            if stack:
                stack.pop()
        elif c in ';{}':
            stack = [] if c != ';' else stack
        if c not in ' \t\r\n':
            last_ident = None if c != '.' and c != '>' and c != ':' else last_ident
        i += 1
    return found


def visible(text):
    """What the player sees of a literal: no ##id, no spaces around"""
    if '##' in text:
        text = text[:text.index('##')]
    # The guide's markers ("# " heading, "- " bullet, "> " menu path) are
    # not drawn
    if text[:2] in ('# ', '- ', '> '):
        text = text[2:]
    return text.strip(' ')


def looks_drawn(t):
    if sum(ch.isalpha() for ch in t) < 2:
        return False
    if '://' in t or t.startswith(('/', '\\', '.')) or '\\' in t:
        return False
    if re.fullmatch(r'[a-z0-9_.\-/]+', t):          # keys, files, ids
        return False
    if re.fullmatch(r'[A-Za-z0-9_]+\.[a-z0-9]{2,4}', t):  # file names
        return False
    if re.fullmatch(r'[A-Z0-9_]{3,}', t) and '_' in t:  # CONSTANT_NAMES
        return False
    if t.startswith(('%', '#')) and not re.search(r'[A-Za-z]{3}', t[2:]):
        return False
    if re.search(r'\b(GL_|SDL_|XEMU_|XBEMU_)', t):
        return False
    # Shell lines, URL pieces, the pieces of built strings
    if re.search(r'\$[A-Za-z({]|&[a-z_-]+=|^-[A-Za-z]|powershell|2>/dev|'
                 r'\bexec\b|Wait-Process|^\)|^#version', t):
        return False
    return True


def found_texts():
    seen, order = {}, []
    for path in SOURCES:
        if not os.path.exists(os.path.join(ROOT, path)):
            continue
        for text, line in texts_in(path):
            t = visible(text)
            if not looks_drawn(t):
                continue
            if t not in seen:
                seen[t] = '%s:%d' % (path, line)
                order.append(t)
    return order, seen


def esc(s):
    return s.replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n')


def unesc(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s):
            n = s[i + 1]
            out.append('\n' if n == 'n' else '\t' if n == 't' else n)
            i += 2
        else:
            out.append(s[i])
            i += 1
    return ''.join(out)


def read_table():
    rows, header = [], LANGS[:]
    if not os.path.exists(TABLE):
        return header, rows
    first = True
    for line in open(TABLE, encoding='utf-8').read().split('\n'):
        if not line or line.startswith('#'):
            continue
        cells = [unesc(c) for c in line.split('\t')]
        if first:
            header, first = cells, False
            continue
        cells += [''] * (len(header) - len(cells))
        rows.append(dict(zip(header, cells)))
    return header, rows


def write_table(header, rows):
    with open(TABLE, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# XBemu translations: see xbemu/tools/lang-table.py\n')
        f.write('\t'.join(header) + '\n')
        for r in rows:
            f.write('\t'.join(esc(r.get(h, '')) for h in header) + '\n')


PLACEHOLDER = re.compile(r'%(?:%|[-+#0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t|L|PRI\w+)?[diouxXeEfgGcsp])')


def placeholders(s):
    # The conversion letter counts; "%.0s" (prints nothing) may stand in for
    # "%s", for example an English plural "s"
    return [p[-1] for p in PLACEHOLDER.findall(s) if p != '%%']


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'check'
    if cmd == 'list':
        order, seen = found_texts()
        for t in order:
            print('%s\t%s' % (seen[t], esc(t)))
        print(len(order), 'texts', file=sys.stderr)
        return 0
    header, rows = read_table()
    if cmd == 'update':
        order, seen = found_texts()
        have = {r['en'] for r in rows}
        added = 0
        for t in order:
            if t not in have:
                rows.append({'en': t})
                added += 1
        gone = [r['en'] for r in rows if r['en'] not in seen]
        write_table(header, rows)
        print('%d rows, %d added, %d no longer in the sources' %
              (len(rows), added, len(gone)))
        for g in gone:
            print('  not found:', esc(g)[:90])
        return 0
    if cmd == 'check':
        bad = 0
        for r in rows:
            want = placeholders(r['en'])
            for lang in header[1:]:
                t = r.get(lang, '')
                if t and placeholders(t) != want:
                    bad += 1
                    print('placeholders differ (%s): %s -> %s' %
                          (lang, esc(r['en'])[:60], esc(t)[:60]))
        for lang in header[1:]:
            missing = sum(1 for r in rows if not r.get(lang))
            print('%s: %d of %d translated' %
                  (lang, len(rows) - missing, len(rows)))
        return 1 if bad else 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
