// Fake frames shaped exactly like klogg::api::ViewportResult.
// The real server fills this from Session::renderViewport(); the spike only
// needs realistic text, chunks, wrap parts and flag bits.
(function () {
  const BASE_FG = '#ffcdd6f4';
  const BASE_BG = '#ff1e1e2e';

  function pad( n, w = 2 ) {
    return String( n ).padStart( w, '0' );
  }

  function makeSource( n ) {
    const ts = `2026-09-27T12:${pad( n % 60 )}:${pad( ( n * 7 ) % 60 )}.${pad(
      ( n * 37 ) % 1000,
      3
    )}Z`;
    const r = n % 20;
    const level = r === 0 || r === 7 ? 'ERROR' : r === 3 || r === 11 ? 'WARN' : 'INFO';

    let message;
    if ( level === 'ERROR' ) {
      message =
        ( n % 2 === 0 ? 'request' : 'job' ) +
        ` ${n} failed: timeout after ${1000 + ( n % 9 ) * 500} ms (retry ${n % 4})`;
    } else if ( level === 'WARN' ) {
      message = `slow query ${300 + ( n % 7 ) * 120} ms on table events (shard ${n % 16})`;
    } else {
      message =
        n % 5 === 0
          ? `worker ${n % 8} heartbeat ok, queue depth ${n % 32}`
          : `GET /api/items/${n} 200 in ${5 + ( n % 90 )} ms, cache=${
              n % 3 === 0 ? 'hit' : 'miss'
            }`;
    }

    // Long lines exercise wrapping and the no-wrap column window.
    if ( n % 9 === 0 ) {
      message +=
        ' payload={"user":"u' +
        ( n % 97 ) +
        '","tags":["alpha","beta","gamma"],"note":"' +
        'lorem ipsum dolor sit amet '.repeat( 6 ) +
        '"}';
    }
    // Wide characters: JS/UTF-16 columns vs rendered width.
    if ( n % 17 === 0 ) {
      message += ' 服务已启动，等待连接；当前队列长度 ' + ( n % 50 );
    }
    // Control characters must never reach xterm raw.
    if ( n % 23 === 0 ) {
      message += ' ctl:bell\u0007 backspace\u0008 nul\u0000 esc\u001b[31mnot-red';
    }

    const text = `${ts} [${level}] [mod${n % 5}] ${message}`;
    const chunks = [];

    chunks.push( { start: 0, length: ts.length, fg: '#ff6c7086', bg: '' } );

    const levelStart = ts.length + 1;
    const levelColor = level === 'ERROR' ? '#fff38ba8' : level === 'WARN' ? '#fff9e2af' : '#ffa6e3a1';
    chunks.push( {
      start: levelStart,
      length: level.length + 2,
      fg: levelColor,
      bg: '',
      bold: true,
    } );

    const timeout = text.indexOf( 'timeout' );
    if ( timeout >= 0 ) {
      chunks.push( {
        start: timeout,
        length: 'timeout'.length,
        fg: '#ffffffff',
        bg: '#ff5a1d1d',
      } );
      chunks.push( {
        start: timeout + 'timeout'.length,
        length: ' after ' + String( 1000 + ( n % 9 ) * 500 ).length,
        fg: '#ffffffff',
        bg: '#ff5a1d1d',
      } );
    } else if ( n % 13 === 0 ) {
      const payload = text.indexOf( 'payload' );
      if ( payload >= 0 ) {
        chunks.push( { start: payload, length: 7, fg: '#fff9e2af', bg: '' } );
      }
    }

    let flags = 0;
    if ( n % 7 === 0 ) flags |= 1; // LineFlagMark
    if ( n % 13 === 0 ) flags |= 2; // LineFlagMatch
    if ( n % 31 === 0 ) flags |= 4; // LineFlagEvent

    return {
      text,
      chunks: chunks.sort( ( a, b ) => a.start - b.start ),
      flags,
      backColor: n % 31 === 0 ? '#ff313244' : BASE_BG,
      foreColor: BASE_FG,
    };
  }

  function wrapParts( text, width ) {
    if ( width <= 0 ) {
      return [ { line: 0, start: 0, length: text.length } ];
    }
    const parts = [];
    let pos = 0;
    let index = 0;
    while ( pos < text.length ) {
      let end = Math.min( text.length, pos + width );
      if ( end < text.length ) {
        // A space exactly at `end` is outside the width; cutting after it
        // would make a part width + 1 columns.
        const space = text.lastIndexOf( ' ', end - 1 );
        if ( space > pos ) {
          end = space + 1;
        }
      }
      parts.push( { line: index++, start: pos, length: end - pos } );
      pos = end;
    }
    if ( parts.length === 0 ) {
      parts.push( { line: 0, start: 0, length: 0 } );
    }
    return parts;
  }

  function buildFrame( { firstLine, visibleLines, wrapColumns } ) {
    const lines = [];
    for ( let i = 0; i < visibleLines; i++ ) {
      const n = firstLine + i;
      const src = makeSource( n );
      lines.push( {
        text: src.text,
        parts: wrapParts( src.text, wrapColumns ),
        chunks: src.chunks,
        lineNumber: n,
        displayLineNumber: n + 1,
        flags: src.flags,
        foreColor: src.foreColor,
        backColor: src.backColor,
      } );
    }
    return { lines };
  }

  window.SpikeSample = { buildFrame, BASE_FG, BASE_BG };
})();
