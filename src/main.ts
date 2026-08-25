import { createApp } from 'vue'
import 'pdfjs-dist/web/pdf_viewer.css'
import './styles/variables.css'
import './styles/base.css'
import './styles/viewer.css'
import './styles/dragdrop.css'
import './styles/eye-care.css'
import './styles/recent-files.css'
import App from './App.vue'
import { initializeEyeCareMode } from './services/eyeCare'
initializeEyeCareMode()
createApp(App).mount('#app')
