import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';

export default defineConfig({
  plugins: [preact()],
  build: {
    outDir: '../data',
    emptyOutDir: false,
    sourcemap: false,
    target: 'es2020',
    rollupOptions: {
      output: {
        entryFileNames: 'assets/[name]-[hash].js',
        chunkFileNames: 'assets/[name]-[hash].js',
        assetFileNames: 'assets/[name]-[hash][extname]',
      },
    },
  },
  server: {
    host: true,
    port: 5173,
    proxy: {
      '/api':     { target: 'http://poolmaster.local', changeOrigin: true },
      '/healthz': { target: 'http://poolmaster.local', changeOrigin: true },
      '/ws':      { target: 'ws://poolmaster.local',  ws: true },
      '/update':  { target: 'http://poolmaster.local', changeOrigin: true },
    },
  },
});
