import { defineConfig } from 'vite';
import { sveltekit } from '@sveltejs/kit/vite';
import adapter from '@sveltejs/adapter-static';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';
export default defineConfig({
  plugins: [sveltekit({ adapter: adapter({pages:'dist',assets:'dist'}), preprocess: vitePreprocess() })],
  server: {proxy: {'/api': {target:'http://127.0.0.1:5600',changeOrigin:true,headers:{Origin:'http://127.0.0.1:5600'}}, '/health':{target:'http://127.0.0.1:5600',changeOrigin:true}}}
});
