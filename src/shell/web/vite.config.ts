import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

export default defineConfig({
  plugins: [svelte()],
  base: './',
  server: {
    host: '127.0.0.1',
    port: 5173,
    proxy: {
      // Dev server talks to a separately started klogg_server.
      '/api': {
        target: 'http://127.0.0.1:8080',
        changeOrigin: false,
      },
    },
  },
  build: {
    // Build output lives in the repository out/ tree, never inside src/.
    outDir: '../../../out/web',
    emptyOutDir: true,
  },
});
