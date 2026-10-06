const {defineConfig}=require('@playwright/test');
module.exports=defineConfig({testDir:'tests/ui',workers:1,use:{baseURL:'http://127.0.0.1:8765',headless:true,
  launchOptions:process.platform==='darwin'?{executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'}:{}},
  webServer:{command:'python3 scripts/preview.py',url:'http://127.0.0.1:8765',reuseExistingServer:false}});
