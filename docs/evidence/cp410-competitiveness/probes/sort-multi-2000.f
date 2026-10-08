a := inject("/home/nick/Repositories/nift/nift/.build/cp49-campaign/extra/records-2000.json")
b := a.sort_by(x => x.v % 7,"asc",x => x.v,"desc"); print(b.length()); print(b[0].v)
; 
