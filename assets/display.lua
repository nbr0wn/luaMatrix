xMax = 192
yMax = 64
r = 3
balls = {}
num_balls = 0

function add_ball()
    local ball = {}
    ball['x'] = math.random(xMax)
    ball['y'] = math.random(yMax)
    ball['dx'] = math.random() - 0.5 
    ball['dy'] = math.random() - 0.5
    ball['r']=math.random(255)
    ball['g']=math.random(255)
    ball['b']=math.random(255)
    table.insert(balls,ball)
end

function tableLength(T)
   local count = 0
   for _ in pairs(T) do count = count + 1 end
   return count
end


function move_balls()
    for k,v in pairs(balls) do
        draw_filled_circle(math.floor(v['x']), math.floor(v['y']), r, 0,0,0)
        v['x'] = v['x'] + v['dx']
        v['y'] = v['y'] + v['dy']
        
        if v['y'] >= yMax-r and v['dy'] > 0 then
            v['y'] = yMax-r
            v['dy'] = -v['dy']
        end
        if v['y'] <= r and v['dy'] < 0 then
            v['y'] = r
            v['dy'] = -v['dy']
        end
        if v['x'] >= xMax-r and v['dx'] > 0 then
            v['x'] = xMax-r
            v['dx'] = -v['dx']
        end
        if v['x'] <= r and v['dx'] < 0 then
            v['x'] = r
            v['dx'] = -v['dx']
        end
        draw_filled_circle(math.floor(v['x']), math.floor(v['y']), r, v['r'], v['g'], v['b'])
    end
end

str="LuaMatrix"

while(1) do
    if tableLength(balls) < 20 then
        add_ball()
    end
    
    --clear_display()
    --draw_string(str,20,20,0,0,0,16)
    move_balls()
    draw_string(str,20,20,200,0,0,16)
    --delay(10)
end