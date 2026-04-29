import { render } from 'preact';
import { App } from './app';
import { bootstrapStores } from './stores';
import './styles.css';

if ('serviceWorker' in navigator && import.meta.env.PROD) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('/sw.js').catch(err => {
      console.warn('SW registration failed', err);
    });
  });
}

bootstrapStores();
render(<App />, document.getElementById('root')!);
