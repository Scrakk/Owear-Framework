import { defineConfig } from 'vite'
import owear from '@owear/vite-plugin'

export default defineConfig({
  plugins: [owear()],
  base: './',
  build: {
    outDir: 'dist',
  },
})
