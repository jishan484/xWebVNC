web_files = [
  ['assets/index.html', 'htmlPage.h'],
  ['assets/login.html', 'loginPage.h'],
  ['assets/vnclogo.PNG', 'logo.h']
]

embedded = []

foreach f : web_files
  embedded += custom_target(
    input: f[0],
    output : f[1], 
    command: ['xxd', '-i', '@INPUT@' , '../@OUTPUT@']
  )
endforeach

assets_sources = embedded
