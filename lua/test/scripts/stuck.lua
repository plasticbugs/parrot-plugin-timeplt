-- stuck.lua: a loop that never ends. The runtime stops it after 5 seconds,
-- and RESET works the whole time.
function update()
  if frame() == 10 then
    local n = 0
    while true do n = n + 1 end
  end
end
function draw() cls() print("ABOUT TO GET STUCK", 8, 8, WHITE) end
