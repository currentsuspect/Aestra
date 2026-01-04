import argparse


CODE = 0
LINE_COMMENT = 1
BLOCK_COMMENT = 2
STRING = 3
CHAR = 4
RAW_STRING = 5


def scan_braces(text, trace_line=None, trace_all=False):
    state = CODE
    raw_delim = ''

    stack = []
    extra_closes = []
    trace_events = []

    line = 1
    col = 0
    i = 0

    while i < len(text):
        ch = text[i]

        if ch == '\n':
            line += 1
            col = 0
            if state == LINE_COMMENT:
                state = CODE
            i += 1
            continue

        col += 1

        if state == CODE:
            nxt = text[i + 1] if i + 1 < len(text) else ''

            if ch == '/' and nxt == '/':
                state = LINE_COMMENT
                i += 2
                col += 1
                continue

            if ch == '/' and nxt == '*':
                state = BLOCK_COMMENT
                i += 2
                col += 1
                continue

            # Raw string: R"delim( ... )delim"
            if ch == 'R' and nxt == '"':
                j = i + 2
                delim_chars = []
                while j < len(text) and text[j] not in ('\n', '('):
                    delim_chars.append(text[j])
                    j += 1

                if j < len(text) and text[j] == '(':
                    raw_delim = ''.join(delim_chars)
                    state = RAW_STRING
                    consumed = j - i + 1
                    i = j + 1
                    col += consumed - 1
                    continue

            if ch == '"':
                state = STRING
                i += 1
                continue

            if ch == "'":
                state = CHAR
                i += 1
                continue

            if ch == '{':
                stack.append((line, col))
            elif ch == '}':
                opened = stack.pop() if stack else None
                if opened is None:
                    extra_closes.append((line, col))

                if trace_all or (trace_line is not None and line == trace_line):
                    trace_events.append((line, col, opened, len(stack)))

            i += 1
            continue

        if state == LINE_COMMENT:
            i += 1
            continue

        if state == BLOCK_COMMENT:
            nxt = text[i + 1] if i + 1 < len(text) else ''
            if ch == '*' and nxt == '/':
                state = CODE
                i += 2
                col += 1
                continue
            i += 1
            continue

        if state == STRING:
            if ch == '\\':
                i += 2
                col += 1
                continue
            if ch == '"':
                state = CODE
            i += 1
            continue

        if state == CHAR:
            if ch == '\\':
                i += 2
                col += 1
                continue
            if ch == "'":
                state = CODE
            i += 1
            continue

        if state == RAW_STRING:
            if ch == ')':
                end = ')' + raw_delim + '"'
                if text.startswith(end, i):
                    state = CODE
                    i += len(end)
                    col += len(end) - 1
                    continue
            i += 1
            continue

    return {
        'final_state': state,
        'unclosed_stack': stack,
        'extra_closes': extra_closes,
        'trace_events': trace_events,
    }


def main():
    parser = argparse.ArgumentParser(
        description='Brace balance + trace tool (ignores comments/strings; supports C++ raw strings).'
    )
    parser.add_argument('path', help='Path to file to scan')
    parser.add_argument('--last', type=int, default=10, help='How many last opens to show when unbalanced')
    parser.add_argument('--trace-line', type=int, default=None, help='1-based line to trace closing braces on')
    parser.add_argument('--trace-all', action='store_true', help='Trace every closing brace (very verbose)')
    args = parser.parse_args()

    text = open(args.path, 'r', encoding='utf-8', errors='replace').read()
    result = scan_braces(text, trace_line=args.trace_line, trace_all=args.trace_all)

    if args.trace_all or args.trace_line is not None:
        for (line, col, opened, depth_after) in result['trace_events']:
            if opened is None:
                print('}} at {}:{} closes NOTHING (extra close), depth_after={}'.format(line, col, depth_after))
            else:
                oline, ocol = opened
                print('}} at {}:{} closes {{ opened at {}:{}, depth_after={}'.format(
                    line, col, oline, ocol, depth_after
                ))

    if result['extra_closes']:
        line, col = result['extra_closes'][0]
        print('Extra } at', line, col)
        return 1

    stack = result['unclosed_stack']
    if stack:
        print('Unclosed { count:', len(stack))
        n = max(1, int(args.last))
        print('Last opens:', stack[-n:])
        return 1

    print('Braces balanced')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
