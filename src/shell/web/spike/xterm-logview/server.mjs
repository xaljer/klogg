// Minimal static server for the spike (no dependencies).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname( fileURLToPath( import.meta.url ) );
const port = Number( process.env.PORT || 5174 );

const types = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.map': 'application/json; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
};

http
  .createServer( ( req, res ) => {
    let pathname;
    try {
      pathname = decodeURIComponent( new URL( req.url, 'http://localhost' ).pathname );
    } catch {
      res.writeHead( 400 ).end( 'bad request' );
      return;
    }
    if ( pathname === '/' ) {
      pathname = '/index.html';
    }
    const file = path.resolve( root, '.' + pathname );
    if ( file !== root && !file.startsWith( root + path.sep ) ) {
      res.writeHead( 403 ).end( 'forbidden' );
      return;
    }
    fs.readFile( file, ( error, data ) => {
      if ( error ) {
        res.writeHead( 404 ).end( 'not found' );
        return;
      }
      res.writeHead( 200, {
        'Content-Type': types[ path.extname( file ) ] || 'application/octet-stream',
        'Cache-Control': 'no-store',
      } );
      res.end( data );
    } );
  } )
  .listen( port, '127.0.0.1', () => {
    console.log( `spike: http://127.0.0.1:${port}/` );
  } );
