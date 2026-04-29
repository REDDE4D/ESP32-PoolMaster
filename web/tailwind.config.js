/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        aqua: {
          bg: {
            900: '#062e4a',
            800: '#0a4565',
            700: '#0e5d75',
          },
          surface: 'rgba(255, 255, 255, 0.07)',
          'surface-elev': 'rgba(255, 255, 255, 0.12)',
          border: 'rgba(125, 211, 252, 0.15)',
          'border-elev': 'rgba(125, 211, 252, 0.25)',
          label: '#7dd3fc',
          primary: '#22d3ee',
          'primary-hover': '#67e8f9',
          text: '#e6f8ff',
          muted: '#64748b',
          ok: '#34d399',
          warn: '#fbbf24',
          alarm: '#f43f5e',
          info: '#22d3ee',
        },
      },
      fontFamily: {
        sans: ['-apple-system', 'BlinkMacSystemFont', '"Segoe UI"', 'Roboto', 'Helvetica', 'Arial', 'sans-serif'],
        mono: ['ui-monospace', 'SFMono-Regular', 'Menlo', 'monospace'],
      },
      backgroundImage: {
        'aqua-gradient': 'linear-gradient(140deg, #062e4a 0%, #0a4565 50%, #0e5d75 100%)',
      },
      backdropBlur: {
        'glass': '10px',
      },
    },
  },
  plugins: [],
};
