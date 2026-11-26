// Run from the pinned alpha2go module directory. This fixture is desktop-only.
package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"net"
	"os"
	"os/exec"
	"time"

	"github.com/knallli/alpha2go/ptpip"
	"github.com/knallli/alpha2go/ptpip/ptpiptest"
)

func main() {
	if len(os.Args) != 2 {
		panic("expected absolute path to sony_ssh_check.exe")
	}
	camera, err := ptpiptest.Start(ptpiptest.Options{Sony: true, Model: "Loopback",
		Props: []ptpip.SonyProp{{Code: 0xd21d, Type: 2, Enabled: 2, Current: ptpip.SonyValue{Int: 1}}}})
	if err != nil {
		panic(err)
	}
	defer camera.Close()
	var secret [24]byte
	if _, err = rand.Read(secret[:]); err != nil {
		panic(err)
	}
	password := hex.EncodeToString(secret[:])
	ssh, err := ptpiptest.StartSSH("fixture", password, camera.Addr())
	if err != nil {
		panic(err)
	}
	defer ssh.Close()
	_, port, err := net.SplitHostPort(ssh.Addr())
	if err != nil {
		panic(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	go func() {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				camera.Emit(0xc203, 0xd21d)
			case <-ctx.Done():
				return
			}
		}
	}()
	for _, mode := range []string{"pin", "denied", "good", "good"} {
		before := ssh.AuthAttempts.Load()
		cmd := exec.CommandContext(ctx, os.Args[1], port, "SHA256:"+ssh.Fingerprint, mode)
		pw := password
		if mode == "denied" {
			pw += "!"
		}
		cmd.Env = append(os.Environ(), "SONY_LOOPBACK_SECRET="+pw)
		cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
		if err := cmd.Run(); err != nil {
			panic(fmt.Errorf("%s: %w", mode, err))
		}
		delta := ssh.AuthAttempts.Load() - before
		if (mode == "pin" && delta != 0) || (mode != "pin" && delta != 1) {
			panic("unexpected authentication attempts")
		}
		fmt.Printf("%s: PASS, server credential attempts=%d\n", mode, delta)
	}
	if len(camera.Controls()) != 0 {
		panic("read-only sessions unexpectedly issued control commands")
	}
}
