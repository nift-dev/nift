fn[async](tiny(id)) { return id+1 }
handles:=[];i:=0;while(i<3000){handles.push(tiny(i));i+=1};sum:=0;for(h:handles){sum+=await h};print(sum)
