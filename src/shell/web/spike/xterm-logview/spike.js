// xterm.js feasibility spike: can a klogg renderViewport() frame be painted
// through xterm, with an aligned gutter, selection mapping, wrapping and a
// no-wrap column window?
(function () {
  const BASE_BG = '#ff1e1e2e';

  const state = {
    firstLine: 0,
    visibleLines: 40,
    wrapColumns: 100,
    firstColumn: 0,
    visibleColumns: 160,
  };

  let term = null;
  let rowMap = [];
  let statusEl = null;
  let followTimer = null;

  function setStatus( text ) {
    statusEl.textContent = text;
  }

  function parseColor( hex ) {
    if ( !hex || hex.length !== 9 || hex[ 0 ] !== '#' ) {
      return null;
    }
    const a = parseInt( hex.slice( 1, 3 ), 16 );
    if ( a === 0 ) {
      return null;
    }
    return {
      r: parseInt( hex.slice( 3, 5 ), 16 ),
      g: parseInt( hex.slice( 5, 7 ), 16 ),
      b: parseInt( hex.slice( 7, 9 ), 16 ),
    };
  }

  function sgr( fg, bg, bold, italic, underline ) {
    let out = '\x1b[0m';
    const f = parseColor( fg );
    const b = parseColor( bg );
    if ( f ) out += `\x1b[38;2;${f.r};${f.g};${f.b}m`;
    if ( b ) out += `\x1b[48;2;${b.r};${b.g};${b.b}m`;
    if ( bold ) out += '\x1b[1m';
    if ( italic ) out += '\x1b[3m';
    if ( underline ) out += '\x1b[4m';
    return out;
  }

  // xterm is a terminal: C0 controls in log text would move the cursor, ring
  // the bell or erase the screen. renderViewport strips ANSI but not every
  // other control byte, so the consumer must sanitize.
  function sanitize( text ) {
    const map = {
      '\x00': '␀',
      '\x07': '␇',
      '\x08': '␈',
      '\x0b': '␋',
      '\x0c': '␌',
      '\x0d': '␍',
      '\x1b': '␛',
    };
    return text
      .replace( /\t/g, '    ' )
      .replace( /[\x00-\x08\x0a-\x1f\x7f]/g, ch => map[ ch ] || '·' );
  }

  // The core lays text out in UTF-16 columns; a terminal lays it out in display
  // cells (CJK = 2). Everything that crosses the boundary goes through these.
  function isWide( codePoint ) {
    return (
      ( codePoint >= 0x1100 && codePoint <= 0x115f ) ||
      ( codePoint >= 0x2e80 && codePoint <= 0xa4cf && codePoint !== 0x303f ) ||
      ( codePoint >= 0xac00 && codePoint <= 0xd7a3 ) ||
      ( codePoint >= 0xf900 && codePoint <= 0xfaff ) ||
      ( codePoint >= 0xfe10 && codePoint <= 0xfe19 ) ||
      ( codePoint >= 0xfe30 && codePoint <= 0xfe6f ) ||
      ( codePoint >= 0xff00 && codePoint <= 0xff60 ) ||
      ( codePoint >= 0xffe0 && codePoint <= 0xffe6 ) ||
      ( codePoint >= 0x1f300 && codePoint <= 0x1f64f ) ||
      ( codePoint >= 0x1f900 && codePoint <= 0x1f9ff ) ||
      ( codePoint >= 0x20000 && codePoint <= 0x3fffd )
    );
  }

  function textCells( text ) {
    let cells = 0;
    for ( const ch of text ) {
      cells += isWide( ch.codePointAt( 0 ) ) ? 2 : 1;
    }
    return cells;
  }

  function splitByCells( text, maxCells ) {
    const pieces = [];
    let start = 0;
    let cells = 0;
    let index = 0;
    while ( index < text.length ) {
      const codePoint = text.codePointAt( index );
      const width = isWide( codePoint ) ? 2 : 1;
      if ( cells > 0 && cells + width > maxCells ) {
        pieces.push( { start, end: index, text: text.slice( start, index ) } );
        start = index;
        cells = 0;
      }
      cells += width;
      index += String.fromCodePoint( codePoint ).length;
    }
    pieces.push( { start, end: text.length, text: text.slice( start ) } );
    return pieces;
  }

  function utf16IndexAtCell( text, targetCell ) {
    let cells = 0;
    let index = 0;
    while ( index < text.length && cells < targetCell ) {
      const codePoint = text.codePointAt( index );
      cells += isWide( codePoint ) ? 2 : 1;
      index += String.fromCodePoint( codePoint ).length;
    }
    return index;
  }

  function buildVisualRows() {
    const frame = window.SpikeSample.buildFrame( state );
    const rows = [];
    for ( const line of frame.lines ) {
      if ( state.wrapColumns > 0 ) {
        const parts = line.parts.length ? line.parts : [ { line: 0, start: 0, length: 0 } ];
        for ( let partIndex = 0; partIndex < parts.length; partIndex++ ) {
          const part = parts[ partIndex ];
          const partText = line.text.slice( part.start, part.start + part.length );
          for ( const piece of splitByCells( partText, state.wrapColumns ) ) {
            rows.push( {
              line,
              partIndex,
              visStart: part.start + piece.start,
              text: piece.text,
            } );
          }
        }
      } else {
        const start = utf16IndexAtCell( line.text, state.firstColumn );
        const window = splitByCells( line.text.slice( start ), state.visibleColumns )[ 0 ];
        rows.push( {
          line,
          partIndex: 0,
          visStart: start + window.start,
          text: window.text,
        } );
      }
    }
    return rows;
  }

  function rowAnsi( row, cols ) {
    const line = row.line;
    const text = row.text;
    const chunks = [];
    for ( const chunk of line.chunks ) {
      const start = Math.max( chunk.start, row.visStart );
      const end = Math.min( chunk.start + chunk.length, row.visStart + text.length );
      if ( end > start ) {
        chunks.push( {
          ...chunk,
          start: start - row.visStart,
          length: end - start,
        } );
      }
    }
    chunks.sort( ( a, b ) => a.start - b.start );

    let out = '';
    let cursor = 0;
    for ( const chunk of chunks ) {
      if ( chunk.start > cursor ) {
        out += sgr( line.foreColor, line.backColor ) + sanitize( text.slice( cursor, chunk.start ) );
      }
      out +=
        sgr(
          chunk.fg || line.foreColor,
          chunk.bg || line.backColor,
          chunk.bold,
          chunk.italic,
          chunk.underline
        ) + sanitize( text.slice( chunk.start, chunk.start + chunk.length ) );
      cursor = Math.max( cursor, chunk.start + chunk.length );
    }
    if ( cursor < text.length ) {
      out += sgr( line.foreColor, line.backColor ) + sanitize( text.slice( cursor ) );
    }
    // Whole-line backgrounds must fill to the right edge, otherwise only the
    // text cells are painted.
    if ( line.backColor && line.backColor !== BASE_BG ) {
      // Fill to the right edge in cells, keeping two spare columns: a cell at
      // the last column arms xterm's auto-wrap and the following CRLF would
      // advance twice.
      out +=
        sgr( line.foreColor, line.backColor ) +
        ' '.repeat( Math.max( 0, cols - 2 - textCells( text ) ) );
    }
    return out + '\x1b[0m';
  }

  function buildFrameData( rows, cols ) {
    let out = '\x1b[0m\x1b[H\x1b[2J';
    for ( let i = 0; i < rows.length; i++ ) {
      out += rowAnsi( rows[ i ], cols );
      if ( i < rows.length - 1 ) {
        out += '\r\n';
      }
    }
    return out;
  }

  function layoutGutter( rows ) {
    const gutter = document.getElementById( 'gutter' );
    const screen = term.element.querySelector( '.xterm-screen' );
    const height = screen ? screen.getBoundingClientRect().height : 0;
    const rowHeight = height > 0 && term.rows > 0 ? height / term.rows : 18;

    gutter.innerHTML = rows
      .map( row => {
        const line = row.line;
        const number = row.partIndex === 0 ? line.displayLineNumber : '↳';
        let marks = '';
        if ( line.flags & 1 ) marks += '<span class="mark">●</span>';
        if ( line.flags & 2 ) marks += '<span class="match">◆</span>';
        if ( line.flags & 4 ) marks += '<span class="event">▮</span>';
        return `<div class="grow" style="height:${rowHeight}px;line-height:${rowHeight}px">
          <span class="num">${number}</span><span class="marks">${marks}</span></div>`;
      } )
      .join( '' );
  }

  function render( done, skipGutter ) {
    // A real viewport has a fixed row count; the server is asked for enough
    // logical lines to fill it. Resizing the terminal every frame scrolls the
    // buffer, so rows stay fixed and extra visual rows are clipped.
    const allRows = buildVisualRows();
    const rows = allRows.slice( 0, term.rows );

    // Rows are wrapped to maxCells display cells; two spare columns keep xterm
    // out of its auto-wrap state.
    const maxCells = state.wrapColumns > 0 ? state.wrapColumns : state.visibleColumns;
    const cols = maxCells + 2;

    term.resize( cols, term.rows );
    rowMap = rows;
    term.write( buildFrameData( rows, cols ), () => {
      if ( !skipGutter ) {
        layoutGutter( rows );
      }
      if ( done ) done();
    } );
  }

  function updateSelection() {
    const position = term.getSelectionPosition();
    if ( !position ) {
      setStatus( 'selection: none' );
      return;
    }
    const start = rowMap[ position.start.y ];
    const end = rowMap[ position.end.y ];
    if ( !start || !end ) {
      setStatus( 'selection: outside the frame' );
      return;
    }
    const column = ( row, cell ) => row.visStart + utf16IndexAtCell( row.text, cell );
    setStatus(
      `selection: L${start.line.lineNumber + 1}:${column( start, position.start.x )} → ` +
        `L${end.line.lineNumber + 1}:${column( end, position.end.x )} (UTF-16 columns)`
    );
  }

  function runPerf() {
    const frames = 300;
    const base = state.firstLine;
    let index = 0;
    const started = performance.now();

    const step = () => {
      if ( index >= frames ) {
        const elapsed = performance.now() - started;
        state.firstLine = base;
        render();
        setStatus(
          `perf: ${frames} frames in ${elapsed.toFixed( 0 )} ms = ` +
            `${( elapsed / frames ).toFixed( 2 )} ms/frame (parse throughput, not paint)`
        );
        return;
      }
      state.firstLine = base + index;
      index++;
      render( step, true );
    };
    step();
  }

  function bindControls() {
    const jump = document.getElementById( 'jump' );
    jump.addEventListener( 'change', () => {
      state.firstLine = Math.max( 0, parseInt( jump.value, 10 ) || 0 );
      render();
    } );

    const wrap = document.getElementById( 'wrap' );
    wrap.checked = state.wrapColumns > 0;
    wrap.addEventListener( 'change', () => {
      state.wrapColumns = wrap.checked ? 100 : 0;
      state.firstColumn = 0;
      document.getElementById( 'hscroll' ).disabled = wrap.checked;
      render();
    } );

    const hscroll = document.getElementById( 'hscroll' );
    hscroll.disabled = state.wrapColumns > 0;
    hscroll.addEventListener( 'input', () => {
      state.firstColumn = parseInt( hscroll.value, 10 ) || 0;
      render();
    } );

    const lines = document.getElementById( 'lines' );
    lines.value = state.visibleLines;
    lines.addEventListener( 'change', () => {
      state.visibleLines = Math.max( 5, Math.min( 300, parseInt( lines.value, 10 ) || 40 ) );
      render();
    } );

    document.getElementById( 'perf' ).addEventListener( 'click', runPerf );

    const follow = document.getElementById( 'follow' );
    follow.addEventListener( 'change', () => {
      if ( followTimer ) {
        clearInterval( followTimer );
        followTimer = null;
      }
      if ( follow.checked ) {
        followTimer = setInterval( () => {
          state.firstLine += 1;
          render();
        }, 500 );
      }
    } );
  }

  function init() {
    statusEl = document.getElementById( 'status' );
    term = new window.Terminal( {
      cols: 120,
      rows: 40,
      scrollback: 0,
      disableStdin: true,
      cursorBlink: false,
      convertEol: false,
      fontSize: 14,
      lineHeight: 1.2,
      fontFamily: '"Cascadia Mono", Consolas, monospace',
      theme: {
        background: '#1e1e2e',
        foreground: '#cdd6f4',
        cursor: '#1e1e2e',
        cursorAccent: '#1e1e2e',
        selectionBackground: '#585b70',
      },
    } );
    term.open( document.getElementById( 'terminal' ) );

    try {
      const addon = new window.WebglAddon.WebglAddon();
      term.loadAddon( addon );
      setStatus( 'renderer: WebGL' );
    } catch ( error ) {
      console.warn( 'WebGL addon unavailable, DOM renderer in use', error );
      setStatus( 'renderer: DOM (WebGL unavailable)' );
    }

    term.onSelectionChange( updateSelection );
    bindControls();
    window.__spike = {
      get term() {
        return term;
      },
      state,
      get rowMap() {
        return rowMap;
      },
      render,
      runPerf,
    };
    render();
    requestAnimationFrame( () => render() );
  }

  window.addEventListener( 'DOMContentLoaded', init );
})();
