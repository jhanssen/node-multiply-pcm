{
  "targets": [
  {
    "conditions": [
      [ 'OS!="win"', {
	"cflags+": [ "-std=c++17" ],
	"cflags_c+": [ "-std=c++17" ],
	"cflags_cc+": [ "-std=c++17" ],
      }],
      [ 'OS=="mac"', {
	"xcode_settings": {
	  "OTHER_CPLUSPLUSFLAGS" : [ "-std=c++17" ],
        },
      }],
    ],
    "include_dirs" : [
      "<!(node -e \"require('nan')\")"
    ],
    "target_name": "multiply-pcm",
    "sources": [ "src/multiply.cpp" ]
  }
  ]
}
