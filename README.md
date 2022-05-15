# WindowLinearTransformer
Simple command line tool to resize / reposition window on Windows

### Usage

Create a `profiles.json` under the dir of this program as follow(remove the comments) :

```jsonc
[
{
    //profile id
    "id": "[ID]",
    //window search condition
    "condition": {
        //window title regex
        "windowTitle": ".+abc",
        //process name regex
        "processName": ".+abc",
        //process id, must be positive integer
        "pid": 123456
    },
    //transform value
    //if x < 0 or y < 0, the position of the window will not change
    //if width < 0 or height < 0, the size of the window will not change
    "pos": {
        //the x coordinate of the window
        "x": 1,
        //the y coordinate of the window
        "y": 2,
        //the width of the window
        "width": -1,
        //the height of the window
        "height": 10
    }
}
//add more profiles...
]
```

Enter `help` to view avaliable command.


### Remark
- For Non-ASCII characters, you need to turn on `Beta: Use Unicode UTF-8 for worldwide language support` in Region -> Administrative
- You may see `AdjustTokenPrivileges failed: Not all privileges or groups referenced are assigned to the caller.` if you are not opening this program with admin privilege. Some of the programs need admin privilege to transform.